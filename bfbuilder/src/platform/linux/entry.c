/*
 * Copyright (C) 2019 Assured Information Security, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/fs.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>

#include <common.h>
#include <bfbuilderinterface.h>

#include <bfdebug.h>
#include <bftypes.h>
#include <bfconstants.h>
#include <bfplatform.h>

/* -------------------------------------------------------------------------- */
/* Misc Device                                                                */
/* -------------------------------------------------------------------------- */

static int
dev_open(struct inode *inode, struct file *file)
{ return 0; }

static int
dev_release(struct inode *inode, struct file *file)
{ return 0; }

static long
ioctl_create_vm_from_bzimage(struct create_vm_from_bzimage_args *args)
{
    int64_t ret;
    struct create_vm_from_bzimage_args kern_args;

    void *bzimage = 0;
    void *initrd = 0;
    void *cmdl = 0;

    if (args == 0) {
        return BF_IOCTL_FAILURE;
    }

    ret = copy_from_user(
        &kern_args, args, sizeof(struct create_vm_from_bzimage_args));
    if (ret != 0) {
        BFALERT("IOCTL_CREATE_VM_FROM_BZIMAGE: failed to copy args from userspace\n");
        return BF_IOCTL_FAILURE;
    }

    if (kern_args.bzimage != 0 && kern_args.bzimage_size != 0) {
        bzimage = platform_alloc_rw(kern_args.bzimage_size);
        if (bzimage == NULL) {
            BFALERT("IOCTL_CREATE_VM_FROM_BZIMAGE: failed to allocate memory for bzimage\n");
            goto failed;
        }

        ret = copy_from_user(bzimage, kern_args.bzimage, kern_args.bzimage_size);
        if (ret != 0) {
            BFALERT("IOCTL_CREATE_VM_FROM_BZIMAGE: failed to copy bzimage from userspace\n");
            goto failed;
        }

        kern_args.bzimage = bzimage;
    }

    if (kern_args.initrd != 0 && kern_args.initrd_size != 0) {
        initrd = platform_alloc_rw(kern_args.initrd_size);
        if (initrd == NULL) {
            BFALERT("IOCTL_CREATE_VM_FROM_BZIMAGE: failed to allocate memory for initrd\n");
            goto failed;
        }

        ret = copy_from_user(initrd, kern_args.initrd, kern_args.initrd_size);
        if (ret != 0) {
            BFALERT("IOCTL_CREATE_VM_FROM_BZIMAGE: failed to copy initrd from userspace\n");
            goto failed;
        }

        kern_args.initrd = initrd;
    }

    if (kern_args.cmdl != 0 && kern_args.cmdl_size != 0) {
        cmdl = platform_alloc_rw(kern_args.cmdl_size);
        if (cmdl == NULL) {
            BFALERT("IOCTL_CREATE_VM_FROM_BZIMAGE: failed to allocate memory for file\n");
            goto failed;
        }

        ret = copy_from_user(cmdl, kern_args.cmdl, kern_args.cmdl_size);
        if (ret != 0) {
            BFALERT("IOCTL_CREATE_VM_FROM_BZIMAGE: failed to copy cmdl from userspace\n");
            goto failed;
        }

        kern_args.cmdl = cmdl;
    }

    ret = common_create_vm_from_bzimage(&kern_args);
    if (ret != BF_SUCCESS) {
        BFDEBUG("common_create_vm_from_bzimage failed: %llx\n", ret);
        goto failed;
    }

    kern_args.bzimage = 0;
    kern_args.initrd = 0;
    kern_args.cmdl = 0;

    ret = copy_to_user(
        args, &kern_args, sizeof(struct create_vm_from_bzimage_args));
    if (ret != 0) {
        BFALERT("IOCTL_CREATE_VM_FROM_BZIMAGE: failed to copy args to userspace\n");
        common_destroy(kern_args.domainid);
        goto failed;
    }

    platform_free_rw(bzimage, kern_args.bzimage_size);
    platform_free_rw(initrd, kern_args.initrd_size);
    platform_free_rw(cmdl, kern_args.cmdl_size);

    return BF_IOCTL_SUCCESS;

failed:

    kern_args.bzimage = 0;
    kern_args.initrd = 0;
    kern_args.cmdl = 0;

    platform_free_rw(bzimage, kern_args.bzimage_size);
    platform_free_rw(initrd, kern_args.initrd_size);
    platform_free_rw(cmdl, kern_args.cmdl_size);

    return BF_IOCTL_FAILURE;
}

static long
ioctl_destroy(domainid_t *args)
{
    int64_t ret;
    domainid_t domainid;

    ret = copy_from_user(&domainid, args, sizeof(domainid_t));
    if (ret != 0) {
        BFALERT("IOCTL_DESTROY: failed to copy args from userspace\n");
        return BF_IOCTL_FAILURE;
    }

    ret = common_destroy(domainid);
    if (ret != BF_SUCCESS) {
        BFDEBUG("common_destroy failed: %llx\n", ret);
        return BF_IOCTL_FAILURE;
    }

    return BF_IOCTL_SUCCESS;
}

static long
dev_unlocked_ioctl(
    struct file *file, unsigned int cmd, unsigned long arg)
{
    switch (cmd) {
        case IOCTL_CREATE_VM_FROM_BZIMAGE:
            return ioctl_create_vm_from_bzimage((struct create_vm_from_bzimage_args *)arg);

        case IOCTL_DESTROY:
            return ioctl_destroy((domainid_t *)arg);

        default:
            return -EINVAL;
    }
}

static struct file_operations fops = {
    .open = dev_open,
    .release = dev_release,
    .unlocked_ioctl = dev_unlocked_ioctl
};

static struct miscdevice builder_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = BUILDER_NAME,
    .fops = &fops,
    .mode = 0666
};

/* -------------------------------------------------------------------------- */
/* Entry / Exit                                                               */
/* -------------------------------------------------------------------------- */

int
dev_init(void)
{
    platform_init();

    if (misc_register(&builder_dev) != 0) {
        BFALERT("misc_register failed\n");
        return -EPERM;
    }

    return 0;
}

void
dev_exit(void)
{
    misc_deregister(&builder_dev);
    return;
}

module_init(dev_init);
module_exit(dev_exit);

MODULE_LICENSE("GPL");
