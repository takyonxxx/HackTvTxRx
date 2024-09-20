#include <stdio.h>
#include <string.h>
#include <stdint.h>  // For fixed-width integer types
#include <stdlib.h>  // For malloc and free
#include <libhackrf/hackrf.h>
#include <pthread.h>
#include <unistd.h>
#include "rf.h"
/* Value from host/libhackrf/src/hackrf.c */
#define TRANSFER_BUFFER_SIZE 262144

#define _GHZ(x) ((uint64_t)(x) * 1000000000)
#define _MHZ(x) ((x) * 1000000)
#define _KHZ(x) ((x) * 1000)
#define _HZ(x) ((x) * 1)

#define DEFAULT_FREQUENCY              _MHZ(100)
#define DEFAULT_SAMPLE_RATE            _MHZ(20)
#define DEFAULT_AUDIO_SAMPLE_RATE      _KHZ(48)
#define DEFAULT_CUT_OFF                _KHZ(75)
#define HACKRF_TX_VGA_MAX_DB            47.0
#define HACKRF_RX_VGA_MAX_DB            40.0
#define HACKRF_RX_LNA_MAX_DB            40.0
#define HACKRF_AMP_MAX_DB               14.0
#define DEFAULT_FFT_SIZE                1024

typedef enum {
    BUFFER_EMPTY,
    BUFFER_PREFILL,
    BUFFER_FILL,
    BUFFER_READY,
} buffer_status_t;

typedef struct {

    /* Buffers are locked while reading/writing */
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    buffer_status_t status;

    /* Pointer to the start of the buffer */
    int8_t *data;

    /* Offset to start of data */
    size_t start;

    /* Length of data ready */
    size_t length;

} buffer_t;

typedef struct {

    buffer_t *buffers;

    int length;
    int count;
    int prefill;
    int in;
    int out;

} buffers_t;

typedef struct {

    /* HackRF device */
    hackrf_device *d;
    /* Buffers */
    buffers_t buffers;
    rxtx_mode mode;

} hackrf_t;

static int _buffer_init(buffers_t *buffers, size_t count, size_t length)
{
    int i;

    buffers->count = count;
    buffers->length = length;
    buffers->buffers = calloc(count, sizeof(buffer_t));

    for(i = 0; i < count; i++)
    {
        pthread_mutex_init(&buffers->buffers[i].mutex, NULL);
        pthread_cond_init(&buffers->buffers[i].cond, NULL);
        buffers->buffers[i].data = malloc(length);
        buffers->buffers[i].start = buffers->length;
        buffers->buffers[i].length = buffers->length;
        buffers->buffers[i].status = BUFFER_EMPTY;
    }

    buffers->prefill = 1;
    buffers->in = 0;
    buffers->out = 0;

    return(0);
}

static int _buffer_free(buffers_t *buffers)
{
    int i;

    for(i = 0; i < buffers->count; i++)
    {
        free(buffers->buffers[i].data);
        pthread_cond_destroy(&buffers->buffers[i].cond);
        pthread_mutex_destroy(&buffers->buffers[i].mutex);
    }

    free(buffers->buffers);
    memset(buffers, 0, sizeof(buffers_t));

    return(0);
}

static int _buffer_read(buffers_t *buffers, int8_t *dst, size_t length)
{
    buffer_t *buf = &buffers->buffers[buffers->out];

    if(buf->start == buffers->length)
    {
        buffer_status_t r;

        /* Check if we can read this block */
        pthread_mutex_lock(&buf->mutex);
        r = buf->status;
        pthread_mutex_unlock(&buf->mutex);

        if(r != BUFFER_READY)
        {
            /* This buffer is not ready - display warning if not in prefill stage */
            if(r != BUFFER_PREFILL)
            {
                fprintf(stderr, "U");
            }

            return(0);
        }

        buf->start = 0;
    }

    if(length > buffers->length - buf->start)
    {
        length = buffers->length - buf->start;
    }

    memcpy(dst, buf->data + buf->start, length);
    buf->start += length;

    if(buf->start == buffers->length)
    {
        /* Flag the current block as avaliable for writing */
        pthread_mutex_lock(&buf->mutex);
        buf->status = BUFFER_EMPTY;
        pthread_mutex_unlock(&buf->mutex);
        pthread_cond_broadcast(&buf->cond);

        buffers->out = (buffers->out + 1) % buffers->count;
    }

    return(length);
}

static size_t _buffer_write_ptr(buffers_t *buffers, int8_t **src)
{
    buffer_t *buf = &buffers->buffers[buffers->in];

    if(buf->length == buffers->length)
    {
        pthread_mutex_lock(&buf->mutex);

        if(buf->status == BUFFER_PREFILL)
        {
            buffers->prefill = 0;
            buf->status = BUFFER_READY;
        }

        while(buf->status != BUFFER_EMPTY)
        {
            pthread_cond_wait(&buf->cond, &buf->mutex);
        }

        pthread_mutex_unlock(&buf->mutex);

        buf->length = 0;
    }

    *src = buf->data + buf->length;

    return(buffers->length - buf->length);
}

static int _buffer_write(buffers_t *buffers, size_t length)
{
    buffer_t *buf = &buffers->buffers[buffers->in];

    buf->length += length;

    if(buf->length == buffers->length)
    {
        pthread_mutex_lock(&buf->mutex);
        buf->status = (buffers->prefill ? BUFFER_PREFILL : BUFFER_READY);
        pthread_mutex_unlock(&buf->mutex);

        buffers->in = (buffers->in + 1) % buffers->count;
    }

    return(length);
}

static int _tx_callback(hackrf_transfer *transfer)
{
    hackrf_t *rf = transfer->tx_ctx;
    size_t l = transfer->valid_length;
    uint8_t *buf = transfer->buffer;
    int r;

    while (l)
    {
        r = _buffer_read(&rf->buffers, (int8_t *)buf, l);

        if (r < 0)
        {
            // Error reading buffer
            printf("Error: Buffer read failed with code %d\n", r);
            return -1; // Return an error code
        }
        else if (r == 0)
        {
            // Buffer underrun, fill with zeros
            memset(buf, 0, l);
            l = 0;
        }
        else
        {
            //printf("Copied %d bytes to TX buffer\n", r);
            l -= r;
            buf += r;
        }
    }

    return 0;
}

static int _rx_callback(hackrf_transfer *transfer)
{
    hackrf_t *rf = transfer->rx_ctx;
    size_t l = transfer->valid_length;
    uint8_t *buf = transfer->buffer;
    int8_t *dst;
    size_t r;

    while(l > 0)
    {
        r = _buffer_write_ptr(&rf->buffers, &dst);
        if(r == 0)
        {
            fprintf(stderr, "O");
            break;
        }
        if(r > l) r = l;
        memcpy(dst, buf, r);
        _buffer_write(&rf->buffers, r);
        l -= r;
        buf += r;
    }
    return(0);
}

static int _rf_write(void *private, int16_t *iq_data, size_t samples)
{
    hackrf_t *rf = private;
    int8_t *iq8 = NULL;
    int i, r;

    samples *= 2;

    while(samples > 0)
    {
        r = _buffer_write_ptr(&rf->buffers, &iq8);

        for(i = 0; i < r && i < samples; i++)
        {
            iq8[i] = iq_data[i] >> 8;
        }

        _buffer_write(&rf->buffers, i);

        iq_data += i;
        samples -= i;
    }

    return(RF_OK);
}

static int _rf_read(void *private, int16_t *iq_data, size_t samples)
{
    hackrf_t *rf = private;
    int8_t iq8[TRANSFER_BUFFER_SIZE];  // Stack'te bir buffer oluştur
    size_t i, r, total_read = 0;
    samples *= 2;  // Each sample is I and Q, so double the count

    while(samples > 0)
    {
        r = _buffer_read(&rf->buffers, iq8, samples > TRANSFER_BUFFER_SIZE ? TRANSFER_BUFFER_SIZE : samples);
        if(r == 0)
        {
            break;  // No more data available
        }
        for(i = 0; i < r; i++)
        {
            iq_data[total_read + i] = ((int16_t)iq8[i]) << 8;
        }
        total_read += r;
        samples -= r;
    }
    return (total_read / 2);  // Return the number of I/Q samples read
}

static int _rf_close(void *private)
{
    hackrf_t *rf = private;
    int r;

    if(rf->mode == RX_MODE)
    {
        r = hackrf_stop_rx(rf->d);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_stop_rx() failed: %s (%d)\n", hackrf_error_name(r), r);
            return(RF_ERROR);
        }
    }
    else
    {       
        r = hackrf_stop_tx(rf->d);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_stop_tx() failed: %s (%d)\n", hackrf_error_name(r), r);
            return(RF_ERROR);
        }
    }

    /* Wait until streaming has stopped */
    while(hackrf_is_streaming(rf->d) == HACKRF_TRUE)
    {
        usleep(100);
    }

    r = hackrf_close(rf->d);
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_close() failed: %s (%d)\n", hackrf_error_name(r), r);
    }

    hackrf_exit();

    _buffer_free(&rf->buffers);
    free(rf);

    return(RF_OK);
}

int rf_hackrf_open(
    rxtx_mode mode, rf_t *s,
    const char *serial,
    uint32_t sample_rate,
    uint64_t frequency_hz,
    unsigned char amp_enable)
{
    hackrf_t *rf;
    int r;
    uint8_t rev;    

    rf = calloc(1, sizeof(hackrf_t));
    if(!rf)
    {
        return(RF_OUT_OF_MEMORY);
    }

    rf->mode = mode;

    /* Print the library version number */
    fprintf(stderr, "libhackrf version: %s (%s)\n",
            hackrf_library_release(),
            hackrf_library_version());

    /* Prepare the HackRF for output */
    r = hackrf_init();
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_init() failed: %s (%d)\n", hackrf_error_name(r), r);
        free(rf);
        return(RF_ERROR);
    }

    r = hackrf_open_by_serial(serial, &rf->d);
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_open() failed: %s (%d)\n", hackrf_error_name(r), r);
        free(rf);
        return(RF_ERROR);
    }

    /* Print the hardware revision */
    r = hackrf_board_rev_read(rf->d, &rev);
    if(r == HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf: Hardware Revision: %s\n", hackrf_board_rev_name(rev));
    }

    r = hackrf_set_sample_rate_manual(rf->d, sample_rate, 1);
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_sample_rate_set() failed: %s (%d)\n", hackrf_error_name(r), r);
        free(rf);
        return(RF_ERROR);
    }

    r = hackrf_set_baseband_filter_bandwidth(rf->d, hackrf_compute_baseband_filter_bw(sample_rate));
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_baseband_filter_bandwidth_set() failed: %s (%d)\n", hackrf_error_name(r), r);
        free(rf);
        return(RF_ERROR);
    }

    r = hackrf_set_freq(rf->d, frequency_hz);
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_set_freq() failed: %s (%d)\n", hackrf_error_name(r), r);
        free(rf);
        return(RF_ERROR);
    }

    r = hackrf_set_txvga_gain(rf->d, HACKRF_TX_VGA_MAX_DB);
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_set_txvga_gain() failed: %s (%d)\n", hackrf_error_name(r), r);
        free(rf);
        return(RF_ERROR);
    }

    r = hackrf_set_amp_enable(rf->d, amp_enable);
    if(r != HACKRF_SUCCESS)
    {
        fprintf(stderr, "hackrf_set_amp_enable() failed: %s (%d)\n", hackrf_error_name(r), r);
        free(rf);
        return(RF_ERROR);
    }

    /* Allocate memory for the output buffers, enough for at least 400ms - minimum 4 */
    r = sample_rate * 2 * 4 / 10 / TRANSFER_BUFFER_SIZE;
    if(r < 4) r = 4;
    _buffer_init(&rf->buffers, r, TRANSFER_BUFFER_SIZE);   

    if(rf->mode == RX_MODE)
    {
        r =  hackrf_set_lna_gain(rf->d, HACKRF_RX_LNA_MAX_DB);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_set_lna_gain() failed: %s (%d)\n", hackrf_error_name(r), r);
            free(rf);
            return(RF_ERROR);
        }

        r =   hackrf_set_vga_gain(rf->d, HACKRF_RX_VGA_MAX_DB);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, " hackrf_set_vga_gain() failed: %s (%d)\n", hackrf_error_name(r), r);
            free(rf);
            return(RF_ERROR);
        }

        r = hackrf_set_amp_enable(rf->d, 0);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_set_amp_enable() failed: %s (%d)\n", hackrf_error_name(r), r);
            free(rf);
            return(RF_ERROR);
        }

        r = hackrf_set_antenna_enable(rf->d, true);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_set_antenna_enable() failed: %s (%d)\n", hackrf_error_name(r), r);
            free(rf);
            return(RF_ERROR);
        }

        r = hackrf_start_rx(rf->d, _rx_callback, rf);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_start_rx() failed: %s (%d)\n", hackrf_error_name(r), r);
            free(rf);
            return(RF_ERROR);
        }
        printf("hackrf_start_rx() ok\n");
    }
    else if(rf->mode == TX_MODE)
    {
        /* Begin transmitting */
        r = hackrf_start_tx(rf->d, _tx_callback, rf);
        if(r != HACKRF_SUCCESS)
        {
            fprintf(stderr, "hackrf_start_tx() failed: %s (%d)\n", hackrf_error_name(r), r);
            free(rf);
            return(RF_ERROR);
        }
        printf("hackrf_start_tx() ok\n");
    }

    /* Register the callback functions */
    s->ctx = rf;
    s->write = _rf_write;
    s->read = _rf_read;
    s->close = _rf_close;
    return(RF_OK);
}
