#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/statfs.h>
#include <linux/mm.h>
#include <asm/uaccess.h>
#include <asm/errno.h>
#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/ramfs.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/init.h>
#include <linux/backing-dev.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/blkdev.h>
#include <linux/mpage.h>
#include <linux/rmap.h>
#include <linux/percpu.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/sysctl.h>
#include <linux/cpu.h>
#include <linux/syscalls.h>
#include <linux/buffer_head.h> /* __set_page_dirty_buffers */
#include <linux/pagevec.h>
#include <linux/timer.h>
#include <linux/sched/rt.h>
#include <linux/sched/signal.h>
#include <linux/mm_inline.h>
#include <trace/events/writeback.h>

#define TFS_MAGIC 0xabcd
#define FILE_INODE_NUMBER 2

struct inode *tfs_get_inode(struct super_block *sb,
			    const struct inode *dir,
			    umode_t mode,
			    dev_t dev);
static struct dentry *tfs_mount(struct file_system_type *fs_type,
				 int flags,
				 const char *dev_name,
				 void *data);
static void tfs_kill_sb(struct super_block *sb);
static int tfs_fill_super(struct super_block *sb, void *data, int silent);
static unsigned long ramfs_mmu_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags);
static struct inode *tfs_root_inode;
static int tfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
static int tfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev);
static int tfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode);

const struct file_operations tfs_file_operations = {
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.fsync		= noop_fsync,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.llseek		= generic_file_llseek,
	.get_unmapped_area	= ramfs_mmu_get_unmapped_area,
};

const struct inode_operations tfs_file_inode_operations = {
	.setattr	= simple_setattr,
	.getattr	= simple_getattr,
};

static struct file_system_type tfs_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "tfs",
	.mount		= tfs_mount,
	.kill_sb	= tfs_kill_sb,
};

static const struct address_space_operations tfs_aops = {
	.readpage	= simple_readpage,
	.write_begin	= simple_write_begin,
	.write_end	= simple_write_end,
	//.set_page_dirty	= __set_page_dirty_no_writeback,
};

static struct super_operations tfs_sops = {
	.statfs		= simple_statfs,
	.drop_inode	= generic_delete_inode,
};

static struct inode_operations tfs_iops = {
	.create		= tfs_create,
	.lookup		= simple_lookup, // built in lookup
	.link		= simple_link,
	.unlink		= simple_unlink,
	.mkdir		= tfs_mkdir,
	.rmdir		= simple_rmdir,
	.mknod		= tfs_mknod,
	.rename		= simple_rename,
};

static unsigned long ramfs_mmu_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}

static int tfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
	struct inode *inode = tfs_get_inode(dir->i_sb, dir, mode, dev);
	int error = -ENOSPC;

	if (inode)
	{
		d_instantiate(dentry, inode);
		dget(dentry);
		error = 0;
		dir->i_mtime = dir->i_ctime = current_time(dir);
	}

	return error;
}

static int tfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	int retval = tfs_mknod(dir, dentry, mode | S_IFDIR, 0);
	if (!retval)
		inc_nlink(dir);
	return retval;
}

static int tfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	return tfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

struct inode *tfs_get_inode(struct super_block *sb,
			    const struct inode *dir,
			    umode_t mode,
			    dev_t dev)
{
	struct inode *inode = new_inode(sb);

	if (inode)
	{
		inode->i_ino = get_next_ino();
		printk(KERN_INFO "root_inode_number is %lu\n", inode->i_ino);

		inode_init_owner(inode, dir, mode);
		inode->i_mapping->a_ops = &tfs_aops;
		mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
		mapping_set_unevictable(inode->i_mapping);
		inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);	
		switch (mode & S_IFMT) {
			case S_IFREG:
				inode->i_op = &tfs_file_inode_operations;
				inode->i_fop = &tfs_file_operations;
				break;
			case S_IFDIR:
				inode->i_op = &tfs_iops;
				inode->i_fop = &simple_dir_operations;
				break;
		}
	}
	return inode;
}

static struct dentry *tfs_mount (struct file_system_type *fs_type,
				 int flags,
				 const char *dev_name,
				 void *data)
{
	struct dentry *ret;

	ret = mount_nodev(fs_type, flags, data, tfs_fill_super); // for fs with no associated device

	if (unlikely(IS_ERR(ret)))
		printk(KERN_ERR "Error mounting tfs\n");
	else
		printk(KERN_INFO "tfs is successfully mounted on [%s]\n", dev_name);

	return ret;
}

static void tfs_kill_sb(struct super_block *sb)
{
	printk(KERN_INFO "tfs superblock is destroyed. Unmount succesful.\n");

	kill_litter_super(sb);
}

static int tfs_fill_super(struct super_block *sb, void *data, int silent)
{

	printk(KERN_INFO "tfs_fill_super\n");	
	
	sb->s_blocksize 	= 1024;
	sb->s_blocksize_bits 	= 10;
	sb->s_magic		= TFS_MAGIC;
	sb->s_op		= &tfs_sops;
	sb->s_type		= &tfs_fs_type;

	tfs_root_inode		= tfs_get_inode(sb, NULL, S_IFDIR | S_IRWXU, 0);
	if (!(sb->s_root = d_make_root(tfs_root_inode)))
		return -ENOMEM;

	return 0;
}

static int __init tfs_init_fs(void)
{
	int ret;

	ret = register_filesystem(&tfs_fs_type);
	if (likely(ret == 0))
		printk(KERN_INFO "Successfully registered tfs\n");
	else
		printk(KERN_INFO "Failed to register tfs. Error:[%d]", ret);

	return ret;
}

static void __exit tfs_exit_fs(void)
{
	int ret;

	ret = unregister_filesystem(&tfs_fs_type);

	if (likely(ret == 0))
		printk(KERN_INFO "Successfully unregistered tfs\n");
	else
		printk(KERN_ERR "Failed to unregister tfs. Error:[%d]", ret);	
}

MODULE_LICENSE("GPL");	
module_init(tfs_init_fs);
module_exit(tfs_exit_fs);
