#pragma once
#include <iostream>
#include <functional>
#include <cstdarg>
#include <cstdio>
#include <functional>
#include <string>

#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_CYAN    "\033[36m"

// Callback
using LogCallback = std::function<void(const std::string&, bool)>;
extern LogCallback g_logCallback;

inline void print_custom(const char* mdl, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
 
    char buffer[4096];
    int prefixLen = snprintf(buffer, sizeof(buffer), "[ %s ] ", mdl);
    vsnprintf(buffer + prefixLen, sizeof(buffer) - prefixLen, fmt, args);
    
    printf("[ " COLOR_GREEN "%s" COLOR_RESET " ] ", mdl);
    vprintf(fmt, args);
    
    if (g_logCallback) {
        std::string logMsg(buffer);

        if (!logMsg.empty() && logMsg.back() == '\n') {
            logMsg.pop_back();
        }
        g_logCallback(logMsg, true);
    }
    
    va_end(args);
}

inline void print_warning(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    char buffer[4096];
    int prefixLen = snprintf(buffer, sizeof(buffer), "[ warning ] ");
    vsnprintf(buffer + prefixLen, sizeof(buffer) - prefixLen, fmt, args);
    
    printf("[ " COLOR_YELLOW "warning" COLOR_RESET " ] ");
    vprintf(fmt, args);
    
    if (g_logCallback) {
        std::string logMsg(buffer);
        if (!logMsg.empty() && logMsg.back() == '\n') {
            logMsg.pop_back();
        }
        g_logCallback(logMsg, true);
    }
    
    va_end(args);
}

inline void print_info(const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
	
	char buffer[4096];
	int prefixLen = snprintf(buffer, sizeof(buffer), "[ info ] ");
	vsnprintf(buffer + prefixLen, sizeof(buffer) - prefixLen, fmt, args);
	
	printf("[ " COLOR_CYAN "info" COLOR_RESET " ] ");
	vprintf(fmt, args);
	
	if (g_logCallback) {
		std::string logMsg(buffer);
		if (!logMsg.empty() && logMsg.back() == '\n') {
			logMsg.pop_back();
		}
		g_logCallback(logMsg, true);
	}
	
	va_end(args);
}

[[noreturn]] inline void print_error(const std::string& msg) {
    printf("[ " COLOR_RED "error" COLOR_RESET " ] %s", msg.c_str());
    
    if (g_logCallback) {
        std::string logMsg = "[ error ] " + msg;
        if (!logMsg.empty() && logMsg.back() == '\n') {
            logMsg.pop_back();
        }
        g_logCallback(logMsg, false);
    }
    
    std::stringstream ss;
    ss << msg;
    throw std::runtime_error(ss.str());
}

#define error_handling(condition, from, text) \
    try { \
        if (condition) { \
            throw std::exception(text); \
        } \
    } catch (std::exception& ex) { \
        MessageBoxA(NULL, ex.what(), from, 0x00000010L); \
        exit(0); \
    } 