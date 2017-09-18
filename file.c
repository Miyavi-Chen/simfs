/*
 * linux/fs/nvmm/file.c
 *
 * Copyright (C) 2013 College of Computer Science,
 * Chonqing University
 *
 * inode operations
 *
 */

#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/aio.h>
#include <linux/slab.h>
#include <linux/uio.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include "nvmm.h"
#include "acl.h"
#include "xip.h"
#include "xattr.h"

/*
 * input :
 * @vaddr : start virtual address
 * @iov : io control
 * @base : 
 * @bytes : the size to be copied
 * returns :
 * 0 if success else the left size non copied
 */
static size_t __nvmm_iov_copy_from(char *vaddr, const struct iovec *iov, size_t base, size_t bytes)
{
	size_t copied = 0, left = 0;
	while(bytes){
		char __user *buf = iov->iov_base + base;
		int copy = min(bytes, iov->iov_len - base);

		base = 0;
		left = __copy_from_user(vaddr, buf, copy);
		copied += copy;
		bytes -= copy;
		vaddr += copy;
		iov++;

		if(unlikely(left))
			break;

	}
	return copied -left;
}

/*
 * input :
 * @vaddr : start virtual address
 * @iov : io control
 * @base : 
 * @bytes : the size to be copied
 * returns :
 * 0 if success else the left size non copied
 */

static size_t __nvmm_iov_copy_to(char *vaddr, const struct iovec *iov, size_t base, size_t bytes)
{
	size_t copied = 0, left = 0;
	while(bytes){
		char __user *buf = iov->iov_base + base;
		int copy = min(bytes, iov->iov_len - base);

		base = 0;
		left = __copy_to_user(buf, vaddr, copy);
		copied += copy;
		bytes -= copy;
		vaddr += copy;
		iov++;
		
		if(unlikely(left))
			break;
	}
	return copied - left;
}

/*
 * input :
 * @to : start virtual address
 * @i : io iterator
 * @bytes : the size to be copied
 * returns :
 * 0 if success else the left size non copied
 */


static size_t nvmm_iov_copy_from(void * to, struct iov_iter *i, size_t bytes)
{
	size_t copied;
	if(likely(i->nr_segs == 1)){
		int left;
		char __user *buf = i->iov->iov_base + i->iov_offset;
		left = __copy_from_user(to, buf, bytes);
		copied = bytes - left;
	}else{
		copied = __nvmm_iov_copy_from(to, i->iov, i->iov_offset, bytes);
	}
	return copied;
}

/*
 * input :
 * @from : start virtual address
 * @i : io iterator
 * @bytes : the size to be copied
 * returns :
 * 0 if success else the left size non copied
 */
static size_t nvmm_iov_copy_to(void *from, struct iov_iter *i, size_t bytes)
{
	size_t copied;
	if(likely(i->nr_segs == 1)){
		int left;
		char __user *buf = i->iov->iov_base + i->iov_offset;
		left = __copy_to_user(buf, from, bytes);
		copied = bytes - left;
	}else{
		copied = __nvmm_iov_copy_to(from, i->iov, i->iov_offset, bytes);
	}
	return copied;
}
/*
 * input :
 * @inode : vfs inode, the file to be open
 * @file : file descriptor
 * returns :
 * 0 if success else others
 */
static int nvmm_open_file(struct inode *inode, struct file *filp)
{
	int errval = 0;
	pid_t pid = current->pid;
	errval = nvmm_establish_mapping(inode);
//	printk("the process pid is : %d\n", pid);
	if(errval){
		nvmm_error(inode->i_sb, __FUNCTION__, "can't establish mapping\n");
		return errval;
	}

	filp->f_flags |= O_DIRECT;
	return generic_file_open(inode, filp);
}


/*
 *
 */

static int nvmm_release_file(struct file * file)
{
    struct inode *inode = file->f_mapping->host;
	struct nvmm_inode_info *ni_info;
	unsigned long vaddr;
	int err = 0;
	ni_info = NVMM_I(inode);
	vaddr = (unsigned long)ni_info->i_virt_addr;
	if(vaddr){
		if(atomic_dec_and_test(&ni_info->i_p_counter))
			err = nvmm_destroy_mapping(inode);

//		printk("release, ino = %ld, process num = %d, vaddr = %lx\n", inode->i_ino, (ni_info->i_p_counter).counter, vaddr);

	}else{
//		nvmm_info("the viraddr has already been released, you must have some thing wrong!\n");
		err = -1;
	}
	return err;
}

ssize_t nvmm_direct_IO(int rw, struct kiocb *iocb,
		   const struct iovec *iov,
		   loff_t offset, unsigned long nr_segs)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file->f_mapping->host;
	struct super_block *sb = inode->i_sb;
	ssize_t retval = 0;
	int hole = 0;
	struct iov_iter iter;
	loff_t size;
	void *start_vaddr = NVMM_I(inode)->i_virt_addr + offset;
	size_t length = iov_length(iov, nr_segs);
	unsigned long pages_exist = 0, pages_to_alloc = 0,pages_needed = 0;        
    
	if(rw == READ)
		rcu_read_lock();
	size = i_size_read(inode);

	if(length < 0){
		retval = -EINVAL;
		goto out;
	}

	if((rw == READ)&&(offset + length > size))
		length = size - offset;

	if(!length)
		goto out;
    
	iov_iter_init(&iter, iov, nr_segs, length, 0);
	if(rw == READ){
		if(unlikely(hole)){
			
		}else{
			retval = nvmm_iov_copy_to(start_vaddr, &iter, length);
			if(retval != length){
				retval = -EFAULT;
				goto out;
			}
		}		
	}else if(rw == WRITE) {
        pages_needed = ((offset + length + sb->s_blocksize - 1) >> sb->s_blocksize_bits);
        pages_exist = (size + sb->s_blocksize - 1) >> sb->s_blocksize_bits;
        pages_to_alloc = pages_needed - pages_exist;

		if(pages_to_alloc > 0){
			if (nvmm_alloc_blocks(inode, pages_to_alloc)){
				nvmm_info("alloc blocks failed!\n");
				goto out;
			}
		}
        
		retval = nvmm_iov_copy_from(start_vaddr, &iter, length);
		if(retval != length){
			retval = -EFAULT;
			goto out;
		}
	}

out :
	if(rw == READ)
		rcu_read_unlock();
	return retval;
}

/*
 * input :
 * @flags : 
 * returns :
 * 0 if use direct IO ways, else others.
 */
static int nvmm_check_flags(int flags)
{
	if(!(flags&O_DIRECT))
		return -EINVAL;

	return 0;
}
const struct file_operations nvmm_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.mmap		= generic_file_mmap,
	.open		= nvmm_open_file,
	.nvrelease	= nvmm_release_file,	
	.fsync		= noop_fsync,
	.check_flags	= nvmm_check_flags,
};



#ifdef CONFIG_NVMM_XIP
const struct file_operations nvmm_xip_file_operations = {
/*	.llseek		= generic_file_llseek,
	.read		= xip_file_read,
	.write		= xip_file_write,
	.mmap		= xip_file_mmap,
	.open		= generic_file_open,
	.fsync		= noop_fsync,*/
};
#endif


const struct inode_operations nvmm_file_inode_operations = {
#ifdef CONFIG_NVMMFS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= nvmm_listxattr,
	.removexattr	= generic_removexattr,
#endif
	.setattr	= nvmm_notify_change,
	.get_acl	= nvmm_get_acl,
};

