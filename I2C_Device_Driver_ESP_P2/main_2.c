#include <linux/fs.h>	   // Inode and File types
#include <linux/i2c-dev.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "common_data.h"
#include <linux/errno.h>
#include <linux/unistd.h>
#include <errno.h>

extern int errno; 
int main(int argc, char** argv)
{
	int i2c_fd = 0;
	int ret=0;
	long ret2=0;
	int page=0;
	
	i2c_fd = open("/dev/i2c_flash", O_RDWR);
	if(i2c_fd < 0)
	{
		printf("Error: could not open i2c device.\n");
		return -1;
	}
	
	printf("\n\n**************************\n\n Setting Flash to 256th Page Address & getting back again \n\n ***************************\n\n");
	page = 256; 
	ret=-1;
	while(ret<0){
		ret = ioctl(i2c_fd, FLASHSETP, (unsigned long)&page); 
		if(ret < 0)
			printf("Error: could not set page. \t %s\n" , strerror(errno));
	}
	
	printf("\n\n**************************\n\n Getting Flash Current Page Address \n\n *****************************\n\n");
	//page = -1;
	do{
		printf("in ioctl flashgetp");
		page = ioctl(i2c_fd, FLASHGETP, 0);
		if(page == -1 ){ // check if a -1 is recvd
			printf("Error: Could not get page number \t %s \t\t returned value: %lu\n" , strerror(errno) , page);
			usleep(100*1000);
		}
	}while(page == -1);
	printf("From FLASHGETP \t Current page no.(should be 256): %d \n\n", page);
	sleep(7);


	printf("\n\n**************************\n\n Writing to Flash \n\n *****************************\n\n");
	int i=0;
	char writebuff[400*64];

	/* setting up the data */
	for (i=0;i<400;i++) 
		snprintf((writebuff + (i*64)) ,64,"THIS IS THE %d PAGE WRITTEN IN EEPROM BY PRAMEET SINGH KOHLI .........",(i+256)%512);
	
	//for (i=0;i<511;i++)
	//	printf("data: \t %.*s \n" , 64 , (writebuff + (i*64)) );

	//ret=-1;
	do{
		ret = write(i2c_fd, writebuff, 400);
		if(ret < 0){
			printf("Error: could not write. \t %s\n" , strerror(errno));
			usleep(100*1000);
		}
	}while(ret<0);
	
	page = 256;
	/*GOING BACK TO PAGE 256 TO READ FROM THERE*/
	//ret=-1;
	do{
		ret = ioctl(i2c_fd, FLASHSETP, (unsigned long)&page);
		if(ret < 0){
			printf("Error: could not set page. \t %s\n" , strerror(errno));
			usleep(100*1000);
		}
	}while(ret<0);
	printf("Done setting \n");


	printf("\n\n**************************\n\n Displaying 512 pages \n\n *****************************\n\n");

	sleep(1);
	/*READING 512 Pages*/
	char buf2[512*64];

	do{
		ret = read(i2c_fd, buf2, 512);
		if(ret < 0){
			printf("Error: reading 512 pages. \t %s\n" , strerror(errno));
			usleep(100*1000);
		}
	}while(ret < 0);

	printf("\nDone Reading\n");

	for(i=0;i<512;i++){
		printf("page: %d\t %.*s\n",(i + 256) %512,64,buf2+(i*64));
		usleep(100*1000);
	}
	
	printf("\n\n**************************\n\n ERASING \n\n *****************************\n\n");
	
	/* PERFORMING FLASH ERASE: SETS '1' (ASCII VALUE OF 1) TO ALL BYTES in ALL PAGES OF THE EEPROM */
	ret2 = ioctl(i2c_fd, FLASHERASE, 0);
	if( ret2 < 0 ){
		printf("Flash couldnt erase !!\t %s \n" , strerror(errno));
		usleep(100*1000);
	}

	printf("\n\n************************\n\n READING ALL 512 pages up & using FLASHGETS TO CHECK STATUS \n\n **************************\n\n");
	page = 0;
	/*GOING BACK TO PAGE 0 TO READ FROM THERE*/

	do{
		ret = ioctl(i2c_fd, FLASHSETP, (unsigned long)&page);
		if(ret < 0){
			printf("Error: could not set page. \t %s\n" , strerror(errno));
			usleep(100*1000);
		}
	}while(ret<0);

	printf("Done setting \n");
	sleep(1);

	/*READING 512 Pages*/
	char buf5[512*64];
	ret = -1 ;
	
	/*	 FLASHGETS TO CHECK IF DEVICE IS FREE	*/
	ret = read(i2c_fd, buf5, 512);
	while( ioctl(i2c_fd, FLASHGETS, 0) < 0 ){  
		ret = read(i2c_fd, buf5, 512);
		printf("Flash busy, couldnt read 512 pages !!\t %s \t %d \n" , strerror(errno) , ioctl(i2c_fd, FLASHGETS, 0));
		usleep(500*100);
	}
	printf("\nDone Reading\n");

	for(i=0;i<512;i++){
		printf("page: %d\t %.*s\n",i,64,buf5+(i*64));
		usleep(100*1000);
	}
	
	close(i2c_fd);
	return 0;
}
