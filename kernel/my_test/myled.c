#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#include <asm/io.h>

static dev_t dev_id;
 static struct cdev * led_dev;
// static struct class* led_class;
//make interface function done
static int my_led_on (struct inode* innode, struct file* file)
{
    printk("led_on");
    return 0;
}

static int my_led_off (struct inode* innode, struct file* file)
{
    printk("led_off");
    return 0;
}

//create file_operations to link the function 
static struct file_operations led_fops = {
    .owner = THIS_MODULE,
    .open = my_led_on,
    .write = my_led_off,
};

static __init int my_led_init(void)
{
    //申请一个设备号
    alloc_chrdev_region(&dev_id,1,1,"led");
    //申请一个cdev结构体的空间
    led_dev = cdev_alloc();
    //把cdev结构体和led_fops文件函数联系在一起
    cdev_init(led_dev,&led_fops);
    //已经初始化好的cdev添加到系统中
    cdev_add(led_dev,dev_id,1);
    //组测设备节点等都没有设置...
    return 0;
}
static __exit void my_led_exit(void)
{
    //释放设备号和注册的cdev
    cdev_del(led_dev);
    kfree(led_dev);
    unregister_chrdev_region(dev_id,1);
}

module_init(my_led_init);
module_exit(my_led_exit);
MODULE_LICENSE("GPL");