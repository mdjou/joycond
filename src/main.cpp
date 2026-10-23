#include <iostream>
#include "ctlr_mgr.h"
#include "epoll_mgr.h"
#include "joycond_config.h"
#if defined(ANDROID) || defined(__ANDROID__)
#include "ctlr_detector_android.h"
#include "android_log.h"
#else
#include "ctlr_detector_udev.h"
#endif

int main(int argc, char *argv[])
{
#if defined(ANDROID) || defined(__ANDROID__)
    std::cout.rdbuf(new androidbuf); // Redirect cout to logcat
#endif

    joycond_config config = load_config("/etc/joycond.conf");
    std::cout << "config: swap_ab_xy=" << (config.swap_ab_xy ? "true" : "false") << std::endl;

    epoll_mgr epoll_manager;
    ctlr_mgr ctlr_manager(epoll_manager, config);
#if defined(ANDROID) || defined(__ANDROID__)
    ctlr_detector_android android_detector(ctlr_manager, epoll_manager);
#else
    ctlr_detector_udev udev_detector(ctlr_manager, epoll_manager);
#endif

    while (true) {
        epoll_manager.loop();
    }

    return 0;
}
