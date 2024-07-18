#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bio.h>
#include <linux/device-mapper.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

#define DM_MSG_PREFIX "proxy"

struct proxy_target {
    struct dm_dev *dev;
    struct kobject kobj;
    atomic_t error_code;
};

static int proxy_map(struct dm_target *ti, struct bio *bio)
{
    struct proxy_target *pt = ti->private;
    int error = atomic_read(&pt->error_code);

    if (error) {
        bio->bi_status = error;
        bio_endio(bio);
        return DM_MAPIO_SUBMITTED;
    }

    bio_set_dev(bio, pt->dev->bdev);
    return DM_MAPIO_REMAPPED;
}

static ssize_t error_code_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
    struct proxy_target *pt = container_of(kobj, struct proxy_target, kobj);
    return sprintf(buf, "%d\n", atomic_read(&pt->error_code));
}

static ssize_t error_code_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
    struct proxy_target *pt = container_of(kobj, struct proxy_target, kobj);
    int error_code;

    if (kstrtoint(buf, 10, &error_code) < 0)
        return -EINVAL;

    atomic_set(&pt->error_code, error_code);
    return count;
}

static struct kobj_attribute error_code_attribute = __ATTR(error_code, 0644, error_code_show, error_code_store);

static int proxy_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
    struct proxy_target *pt;
    int ret;

    if (argc != 1) {
        ti->error = "Invalid argument count";
        return -EINVAL;
    }

    pt = kzalloc(sizeof(*pt), GFP_KERNEL);
    if (!pt) {
        ti->error = "Out of memory";
        return -ENOMEM;
    }

    ret = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &pt->dev);
    if (ret) {
        ti->error = "Device lookup failed";
        kfree(pt);
        return ret;
    }

    atomic_set(&pt->error_code, 0);

    kobject_init(&pt->kobj, pt->kobj.ktype);
    ret = kobject_add(&pt->kobj, &disk_to_dev(pt->dev->bdev->bd_disk)->kobj, "proxy_target");
    if (ret) {
        ti->error = "Failed to create sysfs entry";
        dm_put_device(ti, pt->dev);
        kfree(pt);
        return ret;
    }

    ret = sysfs_create_file(&pt->kobj, &error_code_attribute.attr);
    if (ret) {
        ti->error = "Failed to create sysfs file";
        kobject_put(&pt->kobj);
        dm_put_device(ti, pt->dev);
        kfree(pt);
        return ret;
    }

    ti->private = pt;
    return 0;
}

static void proxy_dtr(struct dm_target *ti)
{
    struct proxy_target *pt = ti->private;
    sysfs_remove_file(&pt->kobj, &error_code_attribute.attr);
    kobject_put(&pt->kobj);
    dm_put_device(ti, pt->dev);
    kfree(pt);
}

static struct target_type proxy_target = {
    .name = "proxy",
    .version = {1, 0, 0},
    .module = THIS_MODULE,
    .ctr = proxy_ctr,
    .dtr = proxy_dtr,
    .map = proxy_map,
};

static int __init proxy_init(void)
{
    int ret = dm_register_target(&proxy_target);
    if (ret < 0)
        DMERR("register failed %d", ret);
    return ret;
}

static void __exit proxy_exit(void)
{
    dm_unregister_target(&proxy_target);
}

module_init(proxy_init);
module_exit(proxy_exit);

MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Simple proxy device-mapper target with error injection");
MODULE_LICENSE("GPL");
