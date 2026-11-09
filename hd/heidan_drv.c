// heidan_drv.c —— 兼容"黑蛋"协议的最小内核模块
//   设备：/dev/wanbai（misc 设备，自动创建）
//   ioctl: 0x800 INIT_KEY / 0x801 READ_MEM / 0x802 WRITE_MEM / 0x803 MODULE_BASE
//   实现：access_process_vm，不 hook、不 patch，安全
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/mm.h>
#include <linux/slab.h>

#define OP_INIT_KEY     0x800
#define OP_READ_MEM     0x801
#define OP_WRITE_MEM    0x802
#define OP_MODULE_BASE  0x803

#define MAX_IO (1024 * 1024)

struct copy_memory {
    int32_t  pid;
    uint64_t addr;
    void    *buffer;
    uint64_t size;
};

struct module_base {
    int32_t  pid;
    char    *name;
    uint64_t base;
};

static struct task_struct *task_by_pid(int32_t pid)
{
    struct pid *p;
    struct task_struct *t = NULL;
    if (pid <= 0) return NULL;
    p = find_get_pid(pid);
    if (p) {
        t = get_pid_task(p, PIDTYPE_PID);
        put_pid(p);
    }
    return t;
}

static long hd_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
    case OP_INIT_KEY:
        return 0;

    case OP_READ_MEM: {
        struct copy_memory cm;
        struct task_struct *t;
        void *kb;
        int n, ret;

        if (copy_from_user(&cm, (void __user *)arg, sizeof(cm)))
            return -EFAULT;
        if (!cm.size || cm.size > MAX_IO) return -EINVAL;
        t = task_by_pid(cm.pid);
        if (!t) return -ESRCH;
        kb = kmalloc((size_t)cm.size, GFP_KERNEL);
        if (!kb) { put_task_struct(t); return -ENOMEM; }
        n = access_process_vm(t, (unsigned long)cm.addr, kb, (int)cm.size, 0);
        put_task_struct(t);
        if (n <= 0) { kfree(kb); return -EIO; }
        ret = copy_to_user(cm.buffer, kb, (unsigned long)n) ? -EFAULT : 0;
        kfree(kb);
        return ret;
    }

    case OP_WRITE_MEM: {
        struct copy_memory cm;
        struct task_struct *t;
        void *kb;
        int n;

        if (copy_from_user(&cm, (void __user *)arg, sizeof(cm)))
            return -EFAULT;
        if (!cm.size || cm.size > MAX_IO) return -EINVAL;
        kb = kmalloc((size_t)cm.size, GFP_KERNEL);
        if (!kb) return -ENOMEM;
        if (copy_from_user(kb, cm.buffer, (unsigned long)cm.size)) {
            kfree(kb);
            return -EFAULT;
        }
        t = task_by_pid(cm.pid);
        if (!t) { kfree(kb); return -ESRCH; }
        n = access_process_vm(t, (unsigned long)cm.addr, kb, (int)cm.size, FOLL_WRITE);
        put_task_struct(t);
        kfree(kb);
        return (n > 0) ? 0 : -EIO;
    }

    case OP_MODULE_BASE:
        /* 调用方另有 /proc/pid/maps 实现，这里不提供 */
        return -ENOSYS;

    default:
        return -EINVAL;
    }
}

static const struct file_operations hd_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = hd_ioctl,
    .compat_ioctl   = hd_ioctl,
};

static struct miscdevice hd_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "wanbai",
    .fops  = &hd_fops,
    .mode  = 0666,
};

static int __init hd_init(void)
{
    int r = misc_register(&hd_dev);
    if (r) {
        pr_err("heidan: misc_register failed %d\n", r);
        return r;
    }
    pr_info("heidan: /dev/wanbai ready\n");
    return 0;
}

static void __exit hd_exit(void)
{
    misc_deregister(&hd_dev);
    pr_info("heidan: unloaded\n");
}

module_init(hd_init);
module_exit(hd_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("heidan-compatible memory read driver");
MODULE_VERSION("1.0");
