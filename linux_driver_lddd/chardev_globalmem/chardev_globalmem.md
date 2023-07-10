#! https://zhuanlan.zhihu.com/p/642339499
# linux字符设备驱动(1)-思路及是实现
### 思路分析
    字符设备驱动应该是linux最基础的驱动了，首先什么是字符设备呢？
    字符设备就是能够用字节流访问的设备，能够像文件一样打开，读写。那么其实这里就有一条很重要的读写链了，也是我认为理解的关键.
    当用户想要使用一个字符设备时，函数的调用层次为：用户空间->系统调用->VFS(虚拟文件系统)->设备文件->字符设备驱动->字符设备。

#### 1. 关键接口介绍

* 根据上述的函数调用链，linux中就有两个关键的结构体接口，一个是提供给虚拟文件系统调用的file_operations 结构体，里面有很多的文件行为函数指针。那么我们就要实现我们自己字符设备的接口函数。
  
  ```c
  struct file_operations {
	struct module *owner;
	loff_t (*llseek) (struct file *, loff_t, int);
	ssize_t (*read) (struct file *, char __user *, size_t, loff_t *);
	ssize_t (*write) (struct file *, const char __user *, size_t, loff_t *);
	ssize_t (*read_iter) (struct kiocb *, struct iov_iter *);
	ssize_t (*write_iter) (struct kiocb *, struct iov_iter *);
    ...
    }
  ```
* 还有一个接口就是面向linux字符设备管理的cdev,就是将字符设备提供给linux管理：
  
  ```c
  struct cdev {
  	struct kobject kobj;              //内嵌的kobject 对象
  	struct module *owner;             //cdev 所属的模块，一般为THIS_MOUDLE
  	const struct file_operations *ops;//cdev 关联的上述文件操作函数接口
  	struct list_head list;            //链表
  	dev_t dev;                       //设备号
  	unsigned int count;              //同一主设备的个数
  } __randomize_layout;
  ```
* 接上所诉。linux提供了一套管理字符设备的接口。
    * cdev_init() 将字符设备和你创建的file_operations 结构绑定在一起，即将这种字符设备的操作函数绑定为你写的操作函数。
      
      ```c
      void cdev_init(struct cdev *cdev, const struct file_operations *fops)
      {
      	memset(cdev, 0, sizeof *cdev);
      	INIT_LIST_HEAD(&cdev->list);
      	kobject_init(&cdev->kobj, &ktype_cdev_default);
      	cdev->ops = fops;
      }
      ```
  *  cdev_alloc 申请字符设备空间
     
      ```c
       struct cdev *cdev_alloc(void)
        {
        	struct cdev *p = kzalloc(sizeof(struct cdev), GFP_KERNEL);
        	if (p) {
        		INIT_LIST_HEAD(&p->list);
        		kobject_init(&p->kobj, &ktype_cdev_dynamic);
        	}
        	return p;
        }
        ```
    

  *  申请设备号 可以分为静态申请和动态申请，其中静态申请要自己指定设备号，而动态申请是系统分配主从设备号，主要是下面两个函数。

     ```c
     /*静态申请：
     *from:要申请的设备号中的第一个，必须包含主设备号
     *count:申请的连续的涉笔号的名称
     *name：设备或者驱动程序的名字
     */
     int register_chrdev_region(dev_t from, unsigned count, const char *name)
     {
     	struct char_device_struct *cd;
     	dev_t to = from + count;
     	dev_t n, next;

     	for (n = from; n < to; n = next) {
     		next = MKDEV(MAJOR(n)+1, 0);
     		if (next > to)
     			next = to;
     		cd = __register_chrdev_region(MAJOR(n), MINOR(n),
     			       next - n, name);
     		if (IS_ERR(cd))
     			goto fail;
     	}
     	return 0;
     fail:
     	to = n;
     	for (n = from; n < to; n = next) {
     		next = MKDEV(MAJOR(n)+1, 0);
     		kfree(__unregister_chrdev_region(MAJOR(n), MINOR(n), next - n));
     	}
     	return PTR_ERR(cd);
     }

     /*动态申请
      * @dev：第一个分配号码的输出参数
      * @baseminor：请求的次要号码范围中的第一个
      * @count：所需的次要号码数
      * @name：关联设备或驱动程序的名称
     */
     int alloc_chrdev_region(dev_t *dev, unsigned baseminor, unsigned count,
     			const char *name)
     {
     	struct char_device_struct *cd;
     	cd = __register_chrdev_region(0, baseminor, count, name);
     	if (IS_ERR(cd))
     		return PTR_ERR(cd);
     	*dev = MKDEV(cd->major, cd->baseminor);
     	return 0;
     }
     ```
  *  cdev_add cdev_del，添加和删除设备,这一步就把字符设备的管理结构体cdev和你刚刚申请的设备节点（设备号）给对接上了。于是你操作设备节点的时候，就知道是哪个cdev了，进而就知道调用哪些file_operations 操作了。
  
  ```c
  int cdev_add(struct cdev *p, dev_t dev, unsigned count)
  {
  	int error;

  	p->dev = dev;
  	p->count = count;

  	error = kobj_map(cdev_map, dev, count, NULL,
  			 exact_match, exact_lock, p);
  	if (error)
  		return error;

  	kobject_get(p->kobj.parent);

  	return 0;
  }
  ```
* 注销涉笔号，释放空间 操作函数：
  
  ```c
  void unregister_chrdev_region(dev_t from, unsigned count)
  {
  	dev_t to = from + count;
  	dev_t n, next;

  	for (n = from; n < to; n = next) {
  		next = MKDEV(MAJOR(n)+1, 0);
  		if (next > to)
  			next = to;
  		kfree(__unregister_chrdev_region(MAJOR(n), MINOR(n), next - n));
  	}
  }
  ```
#### 2. 模块思维，module_init 和 nodule_exit.
* 在linux中，驱动都是以模块的方式加载和卸载的。只要按照规定格式定义好ini和exit函数，在加载和卸载的时候就会默认的执行对应函数，参考体系如下：
  
  ```c
  #include <linux/module.h>

  static int __init demo_init(void)
  {
      //设备初始化操作，可以做硬件电路初始化，绑定dev_t->cdev->file_operations等工作
  }

  static void __exit demo_exit(void)
  {
      //设备释放，释放、清除等工作
  }

  module_init(demo_init);
  module_exit(demo_exit);
  ```
#### 3. 字符设备驱动思路 -global_mem实例
* 由接口介绍可知，现在是 (dev_t 设备号)~(struct cdev * 字符设备管理)~( struct file_operations 文件操作接口)三个能够串联起来，因此我们编写字符设备驱动时，主要由下面一些步骤：
  1. 编写init和exit函数,实现设备驱动的初始化和释放。
  1. 定义自己的文件操作结构体，并编写对应的操作结构函数。
  2. 申请自己定义的字符驱动设备号。
  3. 申请cdev结构体空间，并且将cdev结构体分别和 file_operations结构体及dev_t绑定。
* global 实列：(后续继续详细介绍)
    ```c
    #include <linux/module.h>
    #include <linux/fs.h>
    #include <linux/init.h>
    #include <linux/cdev.h>
    #include <linux/slab.h>
    #include <linux/uaccess.h>

    #define GLOBALMEM_SIZE  0x1000 
    #define GLOBALMEM_MAJAOR 230

    static int global_major  = GLOBALMEM_MAJAOR;

    static int globalmem_read(void)
    {

    }

    static int globalmem_write(void)
    {

    }
    static const struct file_operations globalmem_ops = {
        .owner = THIS_MODULE;
        .read = globalmem_read;
        .write = globalmem_write;
    };

    struct globalmem_dev {
        struct cdev cdev;
        unsigned char mem[GLOBALMEM_SIZE];
    };



    struct globalmem_dev * globalmem_devp;

    //只有一个设备，默认设备号是0的情况
    static int __init globalmem_init (void)
    {
        int ret;
        //获得设备号,devno是传递参数，ret是返回结果。
        dev_t devno = MKDEV(global_major,0);
        if(global_major){
            ret = register_chrdev_region(devno,1,'globalmem');
        }else {
            ret  =alloc_chrdev_region(&devno,0,1,'globalmem');
            global_major = MKJOR(devno);
        }
        if (ret <0)
            return ret;

        globalmem_devp = kzalloc(sizeof(struct globalmem_dev),GFP_KERNEL);
        if(!globalmem_devp)
        {
            return -ENOMEM;
            goto fail_malloc;
        }

        //globalmem device init and add.
        globalmem_setup_cdev(globalmem_devp,0);
        return 0;

        fail_malloc:
        unregister_chrdev_region(devno,1);
        return ret;
    }

    //注册设备函数,添加设备节点
    static void globalmem_setup_cdev(struct globalmem_dev * dev,int index)
    {
        //get device id
        int err,devno = MKDEV(global_major,index);
        //char device init ,link device and filoperations
        cdev_init( &(dev->cdev),&globalmem_ops);
        dev->cdev.owner = THIS_MODULE;
        //add a char device to the system
        err = cdev_add(&(dev->cdev),devno,1);
        if(err)
            printK("set up globalmem device error\r\n");
    }

    static void __exit globalmem_exit(void)
    {
    //remove a cdev from the system
    cdev_del(&(globalmem_devp->cdev));
    // free the malloc space
    kfree(globalmem_devp);
    //unregister a range of device numbers
    unregister_chrdev_region(MKDEV(global_major,0),1);
    }
    module_exit(globalmem_exit);

    module_init(globalmem_init_init);



    ```