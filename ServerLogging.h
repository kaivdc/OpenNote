#ifndef SERVER_LOGGING_H
#define SERVER_LOGGING_H

#include <string>

bool openServerLog();
void logServerMessage(const std::string& level, const std::string& message);

#endif
