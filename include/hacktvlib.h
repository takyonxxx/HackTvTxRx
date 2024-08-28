#ifndef HACKTVLIB_H
#define HACKTVLIB_H

#include <functional>
#include <string>

class HackTvLib {
public:
    using LogCallback = std::function<void(const std::string&)>;

    HackTvLib();
    ~HackTvLib();
    void start(int argc, char *argv[]);
    void stop();
    void setLogCallback(LogCallback callback);

private:
    LogCallback m_logCallback;
    void log(const char* format, ...);
};


#endif // HACKTVLIB_H
