#include <sys/mount.h>

#include <consts.hpp>
#include <base.hpp>
#include <sys/vfs.h>

#include "init.hpp"

using namespace std;

void FirstStageInit::prepare() {
    prepare_data();
    restore_ramdisk_init();

    if (faccessat(-1, "/sdcard", F_OK, AT_SYMLINK_NOFOLLOW) != 0) {
        xmkdirs("/storage/self", 0755);
        xsymlink("/system/system/bin/init", "/storage/self/primary");
        LOGD("Symlink /storage/self/primary -> /system/system/bin/init\n");
        close(open("/sdcard", O_CREAT | O_WRONLY | O_CLOEXEC, 0644));
        xmount("/data/magiskinit", "/sdcard", nullptr, MS_BIND, nullptr);
        LOGD("Bind mount /sdcard -> /data/magiskinit\n");
    } else {
        // fallback to hexpatch if /sdcard exists
        auto init = mmap_data("/init", true);
        // Redirect original init to magiskinit
        for (size_t off : init.patch(INIT_PATH, REDIR_PATH)) {
            LOGD("Patch @ %08zX [" INIT_PATH "] -> [" REDIR_PATH "]\n", off);
        }
    }
}

void LegacySARInit::first_stage_prep() {
    // Patch init binary
    int src = xopen("/init", O_RDONLY);
    int dest = xopen("/data/init", O_CREAT | O_WRONLY, 0);
    {
        mmap_data init("/init");
        for (size_t off : init.patch(INIT_PATH, REDIR_PATH)) {
            LOGD("Patch @ %08zX [" INIT_PATH "] -> [" REDIR_PATH "]\n", off);
        }
        write(dest, init.buf(), init.sz());
        fclone_attr(src, dest);
        close(dest);
        close(src);
    }
    xmount("/data/init", "/init", nullptr, MS_BIND, nullptr);
}

bool SecondStageInit::prepare() {
    umount2("/init", MNT_DETACH);
    umount2(INIT_PATH, MNT_DETACH); // just in case
    unlink("/data/init");

    // Make sure init dmesg logs won't get messed up
    argv[0] = (char *) INIT_PATH;

    // Some weird devices like meizu, uses 2SI but still have legacy rootfs
    struct statfs sfs{};
    statfs("/", &sfs);
    if (sfs.f_type == RAMFS_MAGIC || sfs.f_type == TMPFS_MAGIC) {
        // We are still on rootfs, so make sure we will execute the init of the 2nd stage
        unlink("/init");
        xsymlink(INIT_PATH, "/init");
        return true;
    }
    return false;
}
