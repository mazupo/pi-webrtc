#ifndef PARSER_H_
#define PARSER_H_

#include "config.h"

class Parser {
  public:
    static void ParseArgs(int argc, char *argv[], AppConfig &args);
    static void ParseDevice(AppConfig &args);
    static void ParseWsUrl(LiveKitConfig &config);
    // Copies the options several modules share into each module config.
    static void CopySharedOptions(AppConfig &args);
};

#endif // PARSER_H_
