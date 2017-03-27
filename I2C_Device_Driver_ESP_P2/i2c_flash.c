#include <linux/module.h>  // Module Defines and Macros (THIS_MODULE)
#include <linux/kernel.h>  // 
#include <linux/fs.h>	   // Inode and File types
#include <linux/cdev.h>    // Character Device Types and functions.
#include <linux/types.h>
#include <linux/slab.h>	   // Kmalloc/Kfree
#include <asm/uaccess.h>   // Copy to/from user space
#include <linux/string.h>
#include <linux/device.h>  // Device Creation / Destruction functions
#include <linux/i2c.h>     // i2c Kernel Interfaces
#include <linux/i2c-dev.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <asm/gpio.h>
#include <linux/ioctl.h>
#include <linux/errno.h>

#include <linux/init.h>
#include <linux/moduleparam.h> // Passing parameters to modules through insmod
#include <linux/workqueue.h>
#include "common_data.h"
#include <linux/unistd.h>
#if HAVE_UNLOCKED_IOCTL
    #include <linux/mutex.h>
	DEFINE_MUTEX(fs_mutex);
#else
    #include <linux/smp_lock.h>
#endif

#define DEVICE_NAME "i2c_flash"  // device name to be created and registered
#define DEVICE_ADDR 0x54

#define I2CMUX 29 // Clear for I2C function
#define BUSY_LED 26


/* per device structure */
struct tmp_dev {
	struct cdev cdev;               /* The cdev structure */
	// Local variables
	struct i2c_client *client;
	struct i2c_adapter *adapter;
	struct workqueue_struct *my_wq;
	char* message;
	uint16_t current_page_number;
	uint16_t pages_read; // count of number of pages to be read
	bool data_present_in_Q;
	bool copy_ready;
} *tmp_devp;

typedef struct {
	struct work_struct my_work;
	struct tmp_dev* workQ_temp_devp;
	
} my_work_t;


static dev_t tmp_dev_number;      /* Allotted device number */
struct class *tmp_dev_class;          /* Tie with the device model */
static struct device *tmp_dev_device;

/* Work Handler Function For Read*/
void my_wq_function(struct work_struct *work)
{
	int ret=0;
	my_work_t* my_work = (my_work_t*)work;
	struct tmp_dev* tmp_devp = my_work->workQ_temp_devp;
	gpio_set_value_cansleep(BUSY_LED, 1);
	ret = i2c_master_recv(tmp_devp->client, tmp_devp->message, tmp_devp->pages_read*64);
	if(ret < 0){
		kfree(work);
		return;
	}
	gpio_set_value_cansleep(BUSY_LED, 0);
	tmp_devp->copy_ready = true;	// Indicates that copy to user can be performed
	kfree(work);
}

/* Work Handler Function For Write*/
void my_wq_function2(struct work_struct *work)
{
	int ret;
	unsigned int loop_count = 0;
	uint16_t pageaddr;
	uint16_t dummy_pageaddr;
	my_work_t* my_work = (my_work_t*)work;
	struct tmp_dev* tmp_devp = my_work->workQ_temp_devp;

	gpio_set_value_cansleep(BUSY_LED, 1);

	pageaddr = 0;
	for(loop_count = 0; loop_count<tmp_devp->pages_read ; loop_count++){ // for every page to be written
		
		char tosend_ip_data[64];
		char tosend[64+2];
		int innercount=0;

		ret=-1;		
		for(innercount = 0; innercount<64 ; innercount++) // reading 64 bytes i.e. preparing data for a page write
			tosend_ip_data[innercount] = *(tmp_devp->message + (loop_count*64) + innercount);
		
		pageaddr = tmp_devp->current_page_number * 64; 
		memcpy(((char*)&dummy_pageaddr), (((char*)(&pageaddr))+1), 1); // preparing page address. 
		memcpy((((char*)&dummy_pageaddr)+1), &pageaddr, 1);
		memcpy(tosend , &dummy_pageaddr , 2);
		
		memcpy(tosend + 2 , tosend_ip_data , 64);
		ret = i2c_master_send(tmp_devp->client, tosend, 66); //send data
		if(ret < 0){
			gpio_set_value_cansleep(BUSY_LED, 0);
			return;
		}
		tmp_devp->current_page_number++; // increament count and reset to 0 if overflow occurs
		if(tmp_devp->current_page_number ==512){
			tmp_devp->current_page_number = 0;
		}
			
		msleep(7);
	}
	
	gpio_set_value_cansleep(BUSY_LED, 0);

	tmp_devp->pages_read = 0;
	tmp_devp->copy_ready = false;	
	kfree(tmp_devp->message);
	mutex_lock(&fs_mutex);
	tmp_devp->data_present_in_Q = false;
	mutex_unlock(&fs_mutex);
	kfree(work);
}


/*
* Open driver
*/
int tmp_driver_open(struct inode *inode, struct file *file)
{
	struct tmp_dev *tmp_devp;

	/* Get the per-device structure that contains this cdev */
	tmp_devp = container_of(inode->i_cdev, struct tmp_dev, cdev);

	/* Easy access to tmp_devp from rest of the entry points */
	file->private_data = tmp_devp;
	
	return 0;
}

/*
 * Release driver
 */
int tmp_driver_release(struct inode *inode, struct file *file)
{
	//struct tmp_dev *tmp_devp = file->private_data;
	
	return 0;
}

/*
 * Write to driver
 */
ssize_t tmp_driver_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	struct tmp_dev *tmp_devp = file->private_data;
	
	int ret;
	mutex_lock(&fs_mutex);
	if(tmp_devp->data_present_in_Q){
		return -EBUSY;
	}
	else{
	my_work_t* work;

	mutex_unlock(&fs_mutex);
	work = (my_work_t*)kmalloc(sizeof(my_work_t), GFP_KERNEL); // alocate work
	work->workQ_temp_devp = tmp_devp;

	tmp_devp->pages_read = count; 
	mutex_lock(&fs_mutex);
	tmp_devp->data_present_in_Q = true;
	mutex_unlock(&fs_mutex);
	tmp_devp->message = kmalloc(sizeof(char)*count*64, GFP_KERNEL);
	ret = copy_from_user(tmp_devp->message, buf, count*64);	 //get data from user to be sent to EEPROm
	if(ret < 0){
		gpio_set_value_cansleep(BUSY_LED, 0);
		return -1;
	}
	INIT_WORK((struct work_struct*)work, my_wq_function2);	
	ret = queue_work(tmp_devp->my_wq, (struct work_struct *)work); // work (writing) is queued
	return 0;
	}
}
/*
 * Read from driver
 */
ssize_t tmp_driver_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	int ret;
	struct tmp_dev *tmp_devp = file->private_data;

	mutex_lock(&fs_mutex);
	if(tmp_devp->copy_ready){ //if copy_ready is true then copy data to user
		ret = copy_to_user(buf,tmp_devp->message, tmp_devp->pages_read*64);	
		if(ret < 0){
			mutex_unlock(&fs_mutex);
			return -1;
		}
		/*	Reinitialize Everything */
		tmp_devp->current_page_number = (tmp_devp->current_page_number + tmp_devp->pages_read)%512;
		tmp_devp->pages_read = 0;
		tmp_devp->copy_ready = false;
		tmp_devp->data_present_in_Q = false;
		kfree(tmp_devp->message);
		mutex_unlock(&fs_mutex);
		return 0;
	}
	else if(tmp_devp->data_present_in_Q){ //if data_present_in_Q is true then return busy
		mutex_unlock(&fs_mutex);
		return -EBUSY;
	}
	else{ // else set up work queue struct to queue a read from EEPROM
		my_work_t* work;
		work = (my_work_t*)kmalloc(sizeof(my_work_t), GFP_KERNEL);
		if(work < 0 ){ 
			mutex_unlock(&fs_mutex);
			return -1;
		}
		tmp_devp->message = kmalloc(sizeof(char)*count*64, GFP_KERNEL);
		if(tmp_devp->message <0){
			mutex_unlock(&fs_mutex);
			return -1;
		}
		tmp_devp->pages_read = count;

		tmp_devp->data_present_in_Q = true;
		mutex_unlock(&fs_mutex);

		work->workQ_temp_devp = tmp_devp;
		INIT_WORK((struct work_struct*)work, my_wq_function);
		ret = queue_work(tmp_devp->my_wq, (struct work_struct *)work);
		return -EAGAIN;
	}
}

long ioctl_i2cflash(struct file *file, unsigned int request, unsigned long arg){
	
	struct tmp_dev *tmp_devp = file->private_data;
	int ret;
	int loop_count;
	char* dummy;
	uint16_t* num;
	uint16_t dummy_pagenum;
	char dummy_tosend[66];
	#if HAVE_UNLOCKED_IOCTL
   	mutex_lock(&fs_mutex);
	#else
   	lock_kernel(); // some versions of linux do not have unlocked IOCTL.
	#endif
		
	switch(request){
		case FLASHGETS: 
			if(tmp_devp->data_present_in_Q){
				#if HAVE_UNLOCKED_IOCTL
		   		mutex_unlock(&fs_mutex);
				#else
		   		unlock_kernel();
				#endif
				return -EBUSY;
			}
			else{
				ret =-1;
				#if HAVE_UNLOCKED_IOCTL
		   		mutex_unlock(&fs_mutex);
				#else
		  	 	unlock_kernel();
				#endif
				return 0;
			}
			break;

		case FLASHSETP:
			if(tmp_devp->data_present_in_Q){
				#if HAVE_UNLOCKED_IOCTL
		   		mutex_unlock(&fs_mutex);
				#else
		   		unlock_kernel();
				#endif
				return -EBUSY;
			}
			gpio_set_value_cansleep(BUSY_LED, 1);	
			num = kmalloc(sizeof(tmp_devp->current_page_number),GFP_KERNEL);
			ret = copy_from_user(num, (void*)arg, 2);
			if(ret < 0){
				gpio_set_value_cansleep(BUSY_LED, 0);
				#if HAVE_UNLOCKED_IOCTL
	   			mutex_unlock(&fs_mutex);
				#else
	  	 		unlock_kernel();
				#endif
				return -1;
			}

			tmp_devp->current_page_number = *(num);
			*(num) = *(num) * 64;
			memcpy(((char*)&dummy_pagenum), (((char*)(num))+1), 1);
			memcpy((((char*)&dummy_pagenum)+1), num, 1);
			ret = i2c_master_send(tmp_devp->client, (void*)&dummy_pagenum, 2);
			
			if(ret < 0){
				kfree(num);
				gpio_set_value_cansleep(BUSY_LED, 0);
				#if HAVE_UNLOCKED_IOCTL
	   			mutex_unlock(&fs_mutex);
				#else
	  	 		unlock_kernel();
				#endif
				return -1;
			}
			kfree(num);
			#if HAVE_UNLOCKED_IOCTL
   			mutex_unlock(&fs_mutex);
			#else
   			unlock_kernel();
			#endif
			gpio_set_value_cansleep(BUSY_LED, 0);
			return 0;
			break;

		case FLASHGETP:
			//msleep(500);
			//printk("\n\ntmp_devp->current_page_number \t %d \n\n", tmp_devp->current_page_number);
			//msleep(500);
			#if HAVE_UNLOCKED_IOCTL
   			mutex_unlock(&fs_mutex);
			#else
   			unlock_kernel();
			#endif
			return tmp_devp->current_page_number;
			break;

		case FLASHERASE:
			if(tmp_devp->data_present_in_Q){
				#if HAVE_UNLOCKED_IOCTL
		   		mutex_unlock(&fs_mutex);
				#else
		   		unlock_kernel();
				#endif
				return -EBUSY;
			}
			gpio_set_value_cansleep(BUSY_LED, 1);
			dummy_pagenum =0;
			dummy = kmalloc(sizeof(char)*64,GFP_KERNEL);
			if(dummy<0){
				gpio_set_value_cansleep(BUSY_LED, 0);
				#if HAVE_UNLOCKED_IOCTL
   				mutex_unlock(&fs_mutex);
				#else
   				unlock_kernel();
				#endif
				return -1;
			}
			msleep(7);
			memset(dummy,'1',sizeof(char)*64);
			for(loop_count = 0; loop_count<512 ; loop_count++){
				
				memcpy(dummy_tosend , ((char *)(&dummy_pagenum))+1 , 1);
				memcpy(dummy_tosend+1 , &dummy_pagenum , 1);
				memcpy(dummy_tosend + 2 , dummy , 64);
				ret = i2c_master_send(tmp_devp->client, dummy_tosend, 66);
				if(ret < 0){
					kfree(dummy);
					gpio_set_value_cansleep(BUSY_LED, 0);
					#if HAVE_UNLOCKED_IOCTL
   					mutex_unlock(&fs_mutex);
					#else
   					unlock_kernel();
					#endif
					return -1;
				}
				
				msleep(7);
				dummy_pagenum = dummy_pagenum + 64;
			}
			kfree(dummy);
			#if HAVE_UNLOCKED_IOCTL
   			mutex_unlock(&fs_mutex);
			#else
  	 		unlock_kernel();
			#endif
			gpio_set_value_cansleep(BUSY_LED, 0);
			return 0;
		break;
	
		default:
			break;
	}

	#if HAVE_UNLOCKED_IOCTL
   	mutex_unlock(&fs_mutex);
	#else
   	unlock_kernel();
	#endif
	gpio_set_value_cansleep(BUSY_LED, 0);
	return -1;
}

/* File operations structure. Defined in linux/fs.h */
static struct file_operations tmp_fops = {
    .owner		= THIS_MODULE,           /* Owner */
    .open		= tmp_driver_open,        /* Open method */
    .release		= tmp_driver_release,     /* Release method */
    .write		= tmp_driver_write,       /* Write method */
    .read		= tmp_driver_read,        /* Read method */
	.unlocked_ioctl		= ioctl_i2cflash,
};

/*
 * Driver Initialization
 */
int __init tmp_driver_init(void)
{
	int ret;
	
	/* Request dynamic allocation of a device major number */
	if (alloc_chrdev_region(&tmp_dev_number, 0, 1, DEVICE_NAME) < 0) {
			printk(KERN_DEBUG "Can't register device\n"); return -1;
	}

	/* Populate sysfs entries */
	tmp_dev_class = class_create(THIS_MODULE, DEVICE_NAME);

	/* Allocate memory for the per-device structure */
	tmp_devp = kmalloc(sizeof(struct tmp_dev), GFP_KERNEL);
		
	if (!tmp_devp) {
		printk("\n\t i2cflash Bad Kmalloc\n"); return -ENOMEM;
	}

	/* Request I/O region */

	/* Connect the file operations with the cdev */
	cdev_init(&tmp_devp->cdev, &tmp_fops);
	tmp_devp->cdev.owner = THIS_MODULE;

	/* Connect the major/minor number to the cdev */
	ret = cdev_add(&tmp_devp->cdev, (tmp_dev_number), 1);

	if (ret) {
		printk("\n\t i2cflash Bad cdev\n");
		return ret;
	}

	/* Send uevents to udev, so it'll create /dev nodes */
	tmp_dev_device = device_create(tmp_dev_class, NULL, MKDEV(MAJOR(tmp_dev_number), 0), NULL, DEVICE_NAME);	

	ret = gpio_request(I2CMUX, "I2CMUX");
	if(ret)
	{
		printk("\n\t i2cflash GPIO %d is not requested.\n", I2CMUX);
	}

	ret = gpio_direction_output(I2CMUX, 0);
	if(ret)
	{
		printk("\n\t i2cflash GPIO %d is not set as output.\n", I2CMUX);
	}
	gpio_set_value_cansleep(I2CMUX, 0); // Direction output didn't seem to init correctly.

	ret = gpio_request(BUSY_LED, "BUSY_LED");
	if(ret)
	{
		printk("\n\t i2cflash GPIO %d is not requested.\n", BUSY_LED);
	}

	ret = gpio_direction_output(BUSY_LED, 0);
	if(ret)
	{
		printk("\n\t i2cflash GPIO %d is not set as output.\n", BUSY_LED);
	}
	gpio_set_value_cansleep(BUSY_LED, 0); // Direction output didn't seem to init correctly.	

	// Create Adapter using:
	tmp_devp->adapter = i2c_get_adapter(0); // /dev/i2c-0
	if(tmp_devp->adapter == NULL)
	{
		printk("\n\t i2cflash Could not acquire i2c adapter.\n");
		return -1;
	}
	
	printk("\n\t i2cflash: init complete\n");
	// Create Client Structure
	tmp_devp->client = (struct i2c_client*) kmalloc(sizeof(struct i2c_client), GFP_KERNEL);
	tmp_devp->client->addr = DEVICE_ADDR; // Device Address (set by hardware)
	snprintf(tmp_devp->client->name, I2C_NAME_SIZE, "i2c_eeprom");
	tmp_devp->client->adapter = tmp_devp->adapter;
	tmp_devp->current_page_number = 0x0000;
	tmp_devp->pages_read = 0;
	tmp_devp->data_present_in_Q = false;
	tmp_devp->copy_ready = false;
	tmp_devp->my_wq = create_workqueue("my_queue");
	return 0;
}
/* Driver Exit */
void __exit tmp_driver_exit(void)
{
	// Close and cleanup
	i2c_put_adapter(tmp_devp->adapter);
	kfree(tmp_devp->client);

	/* Cleanup workqueue */
	flush_workqueue(tmp_devp->my_wq);
	destroy_workqueue(tmp_devp->my_wq);
	
	/* Release the major number */
	unregister_chrdev_region((tmp_dev_number), 1);

	/* Destroy device */
	device_destroy (tmp_dev_class, MKDEV(MAJOR(tmp_dev_number), 0));
	cdev_del(&tmp_devp->cdev);
	kfree(tmp_devp);
	
	/* Destroy driver_class */
	class_destroy(tmp_dev_class);
	
	gpio_free(I2CMUX);
	gpio_free(BUSY_LED);
	printk("\n\t i2cflash: exit complete\n");
}

module_init(tmp_driver_init);
module_exit(tmp_driver_exit);
MODULE_LICENSE("GPL v2");
