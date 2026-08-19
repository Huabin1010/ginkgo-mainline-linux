// Minimal aarch64 initramfs /init: mount userdata ext4, switch_root to /sbin/init.
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *userdata_paths[] = {
	"/dev/block/by-name/userdata",
	"/dev/block/mmcblk0p87",
	"/dev/mmcblk0p87",
	"/dev/disk/by-partlabel/userdata",
	NULL,
};

static void die(const char *msg)
{
	dprintf(STDERR_FILENO, "initramfs: %s\n", msg);
	sleep(5);
	_exit(1);
}

static void mkpath(const char *path)
{
	if (mkdir(path, 0755) && errno != EEXIST)
		die("mkdir failed");
}

static void trigger_block_uevents(void)
{
	DIR *dir = opendir("/sys/class/block");
	struct dirent *ent;

	if (!dir)
		return;

	while ((ent = readdir(dir))) {
		char path[256];
		int fd;

		if (strncmp(ent->d_name, "mmcblk", 6))
			continue;

		snprintf(path, sizeof(path), "/sys/class/block/%s/uevent",
			 ent->d_name);
		fd = open(path, O_WRONLY);
		if (fd < 0)
			continue;
		write(fd, "add\n", 4);
		close(fd);
	}
	closedir(dir);
	usleep(200000);
}

static const char *find_userdata(void)
{
	for (int i = 0; i < 240; i++) {
		trigger_block_uevents();
		for (int j = 0; userdata_paths[j]; j++) {
			if (!access(userdata_paths[j], F_OK))
				return userdata_paths[j];
		}
		usleep(250000);
	}
	return NULL;
}

static void copy_file(const char *src, const char *dst, mode_t mode)
{
	char buf[4096];
	ssize_t n;
	int in, out;

	in = open(src, O_RDONLY);
	if (in < 0)
		return;
	out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (out < 0) {
		close(in);
		return;
	}
	while ((n = read(in, buf, sizeof(buf))) > 0)
		write(out, buf, n);
	close(in);
	close(out);
}

static void mkdir_p(const char *path)
{
	char tmp[512];
	char *p;

	snprintf(tmp, sizeof(tmp), "%s", path);
	for (p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		mkdir(tmp, 0755);
		*p = '/';
	}
	mkdir(tmp, 0755);
}

static void deploy_entry(const char *src, const char *dst);

static void deploy_dir(const char *src, const char *dst)
{
	DIR *dir = opendir(src);
	struct dirent *ent;
	char srcpath[512], dstpath[512];

	if (!dir)
		return;
	while ((ent = readdir(dir))) {
		if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
			continue;
		snprintf(srcpath, sizeof(srcpath), "%s/%s", src, ent->d_name);
		snprintf(dstpath, sizeof(dstpath), "%s/%s", dst, ent->d_name);
		deploy_entry(srcpath, dstpath);
	}
	closedir(dir);
}

static void deploy_entry(const char *src, const char *dst)
{
	struct stat st;
	char link[512];
	ssize_t len;
	const char *slash;

	if (lstat(src, &st))
		return;

	if (S_ISDIR(st.st_mode)) {
		mkdir(dst, st.st_mode & 07777);
		deploy_dir(src, dst);
		return;
	}

	slash = strrchr(dst, '/');
	if (slash) {
		char parent[512];

		snprintf(parent, sizeof(parent), "%.*s", (int)(slash - dst), dst);
		mkdir_p(parent);
	}

	if (S_ISLNK(st.st_mode)) {
		len = readlink(src, link, sizeof(link) - 1);
		if (len < 0)
			return;
		link[len] = '\0';
		symlink(link, dst);
		return;
	}

	if (S_ISREG(st.st_mode))
		copy_file(src, dst, st.st_mode & 07777);
}

static void deploy_overlay(void)
{
	if (access("/overlay", F_OK))
		return;
	deploy_dir("/overlay", "/newroot");
}

int main(void)
{
	const char *rootdev;

	mkpath("/proc");
	mkpath("/sys");
	mkpath("/dev");
	if (mount("proc", "/proc", "proc", 0, NULL))
		die("mount proc failed");
	if (mount("sysfs", "/sys", "sysfs", 0, NULL))
		die("mount sysfs failed");
	if (mount("devtmpfs", "/dev", "devtmpfs", 0, NULL))
		die("mount devtmpfs failed");

	rootdev = find_userdata();
	if (!rootdev)
		die("userdata partition not found");

	mkpath("/newroot");
	if (mount(rootdev, "/newroot", "ext4", 0, NULL))
		die("mount userdata failed");

	deploy_overlay();

	mkpath("/newroot/proc");
	mkpath("/newroot/sys");
	mkpath("/newroot/dev");

	if (mount("/proc", "/newroot/proc", NULL, MS_MOVE, NULL))
		die("move proc failed");
	if (mount("/sys", "/newroot/sys", NULL, MS_MOVE, NULL))
		die("move sys failed");
	if (mount("/dev", "/newroot/dev", NULL, MS_MOVE, NULL))
		die("move dev failed");

	if (chdir("/newroot"))
		die("chdir /newroot failed");
	if (mount(".", "/", NULL, MS_MOVE, NULL))
		die("move root failed");
	if (chroot("."))
		die("chroot failed");

	execl("/sbin/init", "init", NULL);
	die("exec /sbin/init failed");
}
