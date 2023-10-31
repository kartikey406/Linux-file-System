#include <linux/version.h> /* For LINUX_VERSION_CODE */
#include <linux/fs.h> /* For struct super_block */
#include <linux/errno.h> /* For error codes */
#include <linux/buffer_head.h> /* struct buffer_head, sb_bread, ... */
#include <linux/blkdev.h> /* block_size, ... */
#include <linux/string.h> /* For memcpy */
#include <linux/vmalloc.h> /* For vmalloc, ... */
#include <linux/time.h> /* For get_seconds, ... */
#include "real_sfs_ds.h"
#include "real_sfs_ops.h"
#include<linux/statfs.h>

static int read_sb_from_real_sfs(sfs_info_t *info, sfs_super_block_t *sb)
{
	struct buffer_head *bh;
	int sb_block_start=0;

	if (!(bh = sb_bread(info->vfs_sb, sb_block_start)))
	{
		return -EIO;
	}
	memcpy(sb, bh->b_data, sizeof(sfs_super_block_t));
	brelse(bh);
	return 0;
}
static int read_from_real_sfs(sfs_info_t *info, byte4_t block, byte4_t offset, void *buf, byte4_t len)
{
	byte4_t fs_block_size = info->sb.block_size;
	byte4_t bd_block_size = block_size(info->vfs_sb->s_bdev);
	byte4_t abs;
	struct buffer_head *bh;

	// Translating the real SFS block numbering to underlying block device block numbering, for sb_bread()
	abs = block*fs_block_size+offset; /* 6A: Compute the absolute total byte offset */
	block = abs / bd_block_size;
	offset = abs % bd_block_size;
	if (offset + len > bd_block_size) // Should never happen
	{
		return -EINVAL;
	}
	if (!(bh = sb_bread(info->vfs_sb, block)))
	{
		return -EIO;
	}
	printk(KERN_ERR "READING FROM REAL SFS %d \n",block);
	printk(KERN_ERR "FINAL BLOCK %d \n",block);
	printk(KERN_ERR "FINAL OFFSET %d \n",offset);
	printk(KERN_ERR "%%%%%%%");
	memcpy(buf, bh->b_data + offset, len);
	brelse(bh);
	return 0;
}
static int write_to_real_sfs(sfs_info_t *info, byte4_t block, byte4_t offset, void *buf, byte4_t len)
{
	byte4_t fs_block_size = info->sb.block_size;
	byte4_t bd_block_size = block_size(info->vfs_sb->s_bdev);
	byte4_t abs;
	struct buffer_head *bh;

	// Translating the real SFS block numbering to underlying block device block numbering, for sb_bread()
	abs = block*fs_block_size+offset; /* 6B: Compute the absolute total byte offset */
	block = abs / bd_block_size;
	offset = abs % bd_block_size;
	if (offset + len > bd_block_size) // Should never happen
	{
		return -EINVAL;
	}
	if (!(bh = sb_bread(info->vfs_sb, block)))
	{
		return -EIO;
	}
	printk(KERN_ERR "WRITING FROM REAL SFS %d \n",block);
	printk(KERN_ERR "FINAL BLOCK %d \n",block);
	printk(KERN_ERR "FINAL OFFSET %d \n",offset);
	printk(KERN_ERR "%%%%%%%");
	memcpy(bh->b_data + offset, buf, len);
	mark_buffer_dirty(bh);
	brelse(bh);
	return 0;
}
static int read_entry_from_real_sfs(sfs_info_t *info, int ino, sfs_file_entry_t *fe)
{
	return read_from_real_sfs(info, info->sb.entry_table_block_start/* FS Block Number */,
				 ino * sizeof(sfs_file_entry_t)/*  FS Block Offset */, fe, sizeof(sfs_file_entry_t));
}
static int write_entry_to_real_sfs(sfs_info_t *info, int ino, sfs_file_entry_t *fe)
{
	return write_to_real_sfs(info,  info->sb.entry_table_block_start /*  FS Block Number */,
				 ino * sizeof(sfs_file_entry_t) /* FS Block Offset */, fe, sizeof(sfs_file_entry_t));
}
int sfs_get_file_entry(sfs_info_t *info, int vfs_ino, sfs_file_entry_t *fe)
{
	return read_entry_from_real_sfs(info, V2S_INODE_NUM(vfs_ino) /*  Pass our file entry number */, fe);
}
int sfs_update_file_entry(sfs_info_t *info, int vfs_ino, sfs_file_entry_t *fe)
{
	return write_entry_to_real_sfs(info, V2S_INODE_NUM(vfs_ino) /* Pass our file entry number */, fe);
}

int init_browsing(sfs_info_t *info)
{
	byte1_t *used_blocks;
	int i, j;
	sfs_file_entry_t fe;
	int retval;

	if ((retval = read_sb_from_real_sfs(info, &info->sb)) < 0)
	{
		return retval;
	}
	if (info->sb.type!=SIMULA_FS_TYPE /*  Check for validity of Simula File System */)
	{
		printk(KERN_ERR "Invalid SFS detected. Giving up.\n");
		return -EINVAL;
	}

	/* Mark used blocks */
	used_blocks = (byte1_t *)(vmalloc(info->sb.partition_size));
	if (!used_blocks)
	{
		return -ENOMEM;
	}
	for (i = 0; i < info->sb.data_block_start; i++)
	{
		used_blocks[i] = 1;
	}
	for (; i < info->sb.partition_size; i++)
	{
		used_blocks[i] = 0;
	}

	for (i = 0; i < info->sb.entry_count; i++)
	{
		if ((retval = read_entry_from_real_sfs(info, i, &fe)) < 0)
		{
			vfree(used_blocks);
			return retval;
		}
		if (!fe.name[0]) continue;
		for (j = 0; j < SIMULA_FS_DATA_BLOCK_CNT; j++)
		{
			if (fe.blocks[j] == 0) break;
			used_blocks[fe.blocks[j]] = 1;
		}
	}

	info->used_blocks = used_blocks;
	info->vfs_sb->s_fs_info = info;
	spin_lock_init(&info->lock);
	return 0;
}
void shut_browsing(sfs_info_t *info)
{
	if (info->used_blocks)
		vfree(info->used_blocks);
}

int sfs_get_data_block(sfs_info_t *info)
{
	int i;

	spin_lock(&info->lock); // To prevent racing on used_blocks access
	for (i = info->sb.data_block_start; i < info->sb.partition_size; i++)
	{
		if (info->used_blocks[i] == 0)
		{
			info->used_blocks[i] = 1;
			spin_unlock(&info->lock);
			return i;
		}
	}
	spin_unlock(&info->lock);
	return INV_BLOCK;
}
void sfs_put_data_block(sfs_info_t *info, int i)
{
	spin_lock(&info->lock); // To prevent racing on used_blocks access
	info->used_blocks[i] = 0;
	spin_unlock(&info->lock);
}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(3,11,0))
int sfs_list(sfs_info_t *info, struct file *file, void *dirent, filldir_t filldir)
{
	loff_t pos;
	int ino;
	sfs_file_entry_t fe;
	int retval;

	pos = 1; /* Starts at 1 as . is position 0 & .. is position 1 */
	for (ino = 0; ino < info->sb.entry_count; ino++)
	{
		if ((retval = read_entry_from_real_sfs(info, ino /*  Get the ino'th file entry into fe */, &fe)) < 0)
			return retval;
		if (!fe.name[0]) continue;
		pos++; /* Position of this file */
		if (file->f_pos == pos)
		{
			retval = filldir(dirent, fe.name, strlen(fe.name), file->f_pos, S2V_INODE_NUM(ino), DT_REG);
			if (retval)
			{
				return retval;
			}
			file->f_pos++;
		}
	}
	return 0;
}
#else
int sfs_list(sfs_info_t *info, struct file *file, struct dir_context *ctx)
{
	loff_t pos;
	int ino;
	sfs_file_entry_t fe;
	int retval;

	pos = 1; /* Starts at 1 as . is position 0 & .. is position 1 */
	printk("intial value of ctx->pos %d ",ctx->pos);
	for (ino = 0; ino < info->sb.entry_count; ino++)
	{
		if ((retval = read_entry_from_real_sfs(info, ino /*  Get the ino'th file entry into fe */, &fe)) < 0)
			return retval;
		if (!fe.name[0]) continue;
		pos++; /* Position of this file */
		if (ctx->pos == pos)
		{
			printk(KERN_INFO "value of ctx->pos %d ",ctx->pos);
			printk(KERN_INFO "value of pos %d ",pos);
			if (!dir_emit(ctx, fe.name, strlen(fe.name), S2V_INODE_NUM(ino), DT_REG))
			{
				return -ENOSPC;
			}
			ctx->pos++;
		}
	}
	return 0;
}
#endif
int sfs_create(sfs_info_t *info, char *fn, int perms, sfs_file_entry_t *fe)
/* This function is called only if the file doesn't exist */
{
	int ino, free_ino, i;

	free_ino = INV_INODE;
	for (ino = 0; ino < info->sb.entry_count; ino++)
	{
		if (read_entry_from_real_sfs(info, ino, fe) < 0)
			return INV_INODE;
		if (!fe->name[0])
		{
			free_ino = ino;
			break;
		}
	}
	if (free_ino == INV_INODE)
	{
		printk(KERN_ERR "No entries left\n");
		return INV_INODE;
	}

	strncpy(fe->name, fn, SIMULA_FS_FILENAME_LEN);
	fe->name[SIMULA_FS_FILENAME_LEN] = 0;
	fe->size = 0;
	fe->timestamp = 0; /*  timestamp */
	fe->perms = perms; /* Permissions passed */
	for (i = 0; i < SIMULA_FS_DATA_BLOCK_CNT; i++)
	{
		fe->blocks[i] = 0;
	}

	if (write_entry_to_real_sfs(info, free_ino, fe) < 0)
		return INV_INODE;

	return S2V_INODE_NUM(free_ino);
}
int sfs_lookup(sfs_info_t *info, char *fn, sfs_file_entry_t *fe)
{
	int ino;

	for (ino = 0; ino < info->sb.entry_count; ino++)
	{
		if (read_entry_from_real_sfs(info, ino, fe) < 0)
			return INV_INODE;
		if (!fe->name[0]) continue;
		if (strcmp(fe->name, fn) == 0) return S2V_INODE_NUM(ino); /* Return the VFS Inode Number */
	}

	return INV_INODE;
}
int sfs_remove(sfs_info_t *info, char *fn)
{
	int vfs_ino, block_i;
	sfs_file_entry_t fe;

	if ((vfs_ino = sfs_lookup(info, fn, &fe)) == INV_INODE)
	{
		printk(KERN_ERR "File %s doesn't exist\n", fn);
		return INV_INODE;
	}
	/* Free up all allocated blocks, if any */
	for (block_i = 0; block_i < SIMULA_FS_DATA_BLOCK_CNT; block_i++)
	{
		if (!fe.blocks[block_i])
		{
			break;
		}
		sfs_put_data_block(info, fe.blocks[block_i]);
	}
	/*  Initialize fe entry to zero's */
	memset(&fe, 0, sizeof(sfs_file_entry_t));

	if (write_entry_to_real_sfs(info, V2S_INODE_NUM(vfs_ino), &fe) < 0)
		return INV_INODE;

	return vfs_ino;
}
int sfs_update(sfs_info_t *info, int vfs_ino, int *size, int *timestamp, int *perms)
{
	sfs_file_entry_t fe;
	int i;
	int retval;

	if ((retval = sfs_get_file_entry(info, vfs_ino, &fe)) < 0)
	{
		return retval;
	}
	if (size) fe.size = *size;
	if (timestamp) fe.timestamp = *timestamp;
	if (perms && (*perms <= 07)) fe.perms = *perms;

	for (i = (fe.size + info->sb.block_size - 1) / info->sb.block_size; i < SIMULA_FS_DATA_BLOCK_CNT; i++)
	{
		if (fe.blocks[i])
		{
			sfs_put_data_block(info, fe.blocks[i]);
			fe.blocks[i] = 0;
		}
	}

	return sfs_update_file_entry(info, vfs_ino, &fe);
}

int get_fs_stats(sfs_info_t *info,struct kstatfs *buf){
	int i,ino;
	sfs_file_entry_t fe; 
	buf->f_type=info->sb.type;
	buf->f_bsize=info->sb.block_size;
	buf->f_blocks=info->sb.partition_size;
	buf->f_bfree=0;
	for(i=0;i<info->sb.partition_size;i++){
		if(info->used_blocks[i]==0){
			buf->f_bfree++;
		}
	}
	buf->f_bavail=buf->f_bfree;
	buf->f_files=info->sb.entry_count;
	buf->f_ffree=0;
	for(ino=0;ino < info->sb.entry_count;ino++){
		if((read_entry_from_real_sfs(info,ino,&fe))<0){
			return INV_INODE;
		}
		if(!fe.name[0]){
			buf->f_ffree++;
		}
	}
	buf->f_namelen=SIMULA_FS_FILENAME_LEN;
	return 0;
}

int sfs_rename(sfs_info_t *info,char *old_file_name,char *new_file_name){
	int vfs_info;
	sfs_file_entry_t fe;
	if((vfs_info=sfs_lookup(info,old_file_name,&fe))==INV_INODE){
		return -ENOENT;
	}
	strncpy(fe.name,new_file_name,SIMULA_FS_FILENAME_LEN);
	fe.name[SIMULA_FS_FILENAME_LEN]=0;
	if(write_entry_to_real_sfs(info,V2S_INODE_NUM(vfs_info),&fe)<0){
		return -EIO;
	}
	return 0;
}
