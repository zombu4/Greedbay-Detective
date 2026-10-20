// Test helper for tst_plugins: the plugin runner (src/plugins/PluginRunner.h)
// as the app runs it with "--plugin-runner", plus two ways to misbehave.
//
// usage: pluginrunner           one request from stdin, the answer to stdout
//        pluginrunner --abort   ends abnormally at once (a crash)
//        pluginrunner --hang    never answers (a runner that does not stop)
#include "plugins/PluginRunner.h"

#include <QCoreApplication>
#include <QThread>

#include <cstdlib>
#include <cstring>

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    if (argc == 2 && std::strcmp(argv[1], "--abort") == 0)
        std::abort();
    if (argc == 2 && std::strcmp(argv[1], "--hang") == 0) {
        for (;;)
            QThread::sleep(1);
    }
    return app::plugins::runPluginRequestOnStdio();
}
