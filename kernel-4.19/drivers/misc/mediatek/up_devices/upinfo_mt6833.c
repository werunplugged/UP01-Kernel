

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

extern int up_device_dump(struct seq_file *m);

static int info_show(struct seq_file *m, void *v)
{
        up_device_dump(m);
	return 0;
}

static void *info_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void *info_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void info_stop(struct seq_file *m, void *v)
{
}

const struct seq_operations upinfo_op = {
	.start	= info_start,
	.next	= info_next,
	.stop	= info_stop,
	.show	= info_show
};


static int upinfo_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &upinfo_op);
}

static const struct file_operations proc_upinfo_operations = {
	.open		= upinfo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

extern int up_memory_dump(struct seq_file *m);

static int memory_show(struct seq_file *m, void *v)
{
        up_memory_dump(m);
	return 0;
}

const struct seq_operations memoryinfo_op = {
	.start	= info_start,
	.next	= info_next,
	.stop	= info_stop,
	.show	= memory_show
};

static int memoryinfo_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &memoryinfo_op);
}

static const struct file_operations proc_memoryinfo_operations = {
	.open		= memoryinfo_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

 extern int up_cts_dump(struct seq_file *m);
 
 static int cts_show(struct seq_file *m, void *v)
 {
	up_cts_dump(m);
	return 0;
 }
 
 const struct seq_operations ctsinfo_op = {
	 .start  = info_start,
	 .next	 = info_next,
	 .stop	 = info_stop,
	 .show	 = cts_show
 };
 
 static int ctsinfo_open(struct inode *inode, struct file *file)
 {
	 return seq_open(file, &ctsinfo_op);
 }
 
 static const struct file_operations proc_ctsinfo_operations = {
	 .open		 = ctsinfo_open,
	 .read		 = seq_read,
	 .llseek	 = seq_lseek,
	 .release	 = seq_release,
 };

 static struct proc_dir_entry *upinfo_config_proc = NULL;

 int  proc_upinfo_init(void)
{
    printk("%s: entery\n",__func__);  

    upinfo_config_proc=proc_create("upinfo", 0x666, NULL, &proc_upinfo_operations);
	upinfo_config_proc=proc_create("upmeminfo", 0x666, NULL, &proc_memoryinfo_operations);
	upinfo_config_proc=proc_create("upctsinfo", 0x666, NULL, &proc_ctsinfo_operations);

    if (upinfo_config_proc == NULL)
    {
        printk("create_proc_entry upinfo failed ~~~~\n");
        return -1;
    }
	return 0;
}




