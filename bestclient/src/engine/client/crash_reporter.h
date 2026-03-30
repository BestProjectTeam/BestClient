#ifndef ENGINE_CLIENT_CRASH_REPORTER_H
#define ENGINE_CLIENT_CRASH_REPORTER_H

void InitializeCrashReporter(const char *pCrashLogPath);
bool HandleCrashReporterCommandLine(int argc, const char **argv);
void LaunchCrashReporterMonitor(const char *pExecutablePath, const char *pCrashLogPath);
[[noreturn]] void TriggerTestCrash();

#endif
