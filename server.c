#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <fcntl.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <semaphore.h>
#include "common.h"
#include "lib.h"

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct thread_param
{
	time_t begin;
    int seconds_to_run;
    char* fifoname;
};

struct producer_thread_param{
    struct Message* msg;
    time_t begin;
    int seconds_to_run;
};

struct Message* buffer;
int buffer_index = 0;
int buffer_size;

sem_t sem_full, sem_empty;


void stdout_from_fifo(struct Message *msg, char *operation){
	fprintf(stdout,"%ld; %d; %d; %d; %ld; %d; %s \n",time(NULL), msg->rid, msg->tskload, getpid(), pthread_self(),msg->tskres, operation);
}

void move_buffer_elements_back(){
    for (int i = 0; i < buffer_size-1;i++){
        buffer[i] = buffer[i+1];
    }
    buffer_index--;
}

void* thread_consumer(void* arg){
   
   struct Message msg;

   while(true){     
        char* private_fifo_name = (char *) malloc(50 * sizeof(char));

        sem_wait(&sem_empty); //decreases one every time it removes from the buffer, when it reaches 0 the buffer is empty so it block

        msg = buffer[0];
        move_buffer_elements_back();

		sem_post(&sem_full); //increses every time it removes from the buffer, signaling that there is empty space in the buffer so the productors can work
        

        sprintf(private_fifo_name, "/tmp/%d.%ld", msg.pid, msg.tid);

        struct Message* response_msg = malloc(sizeof(struct Message));
        response_msg->pid = getpid();
        response_msg->rid = msg.rid;
        response_msg->tid = pthread_self();
        response_msg->tskload = msg.tskload;
        response_msg->tskres = msg.tskres;
		
        //access the private fifo
        int np = open(private_fifo_name, O_WRONLY | O_NONBLOCK);

        if(np < 0){
            stdout_from_fifo(&msg, "FAILD"); //couldnt open private fifo, it was already deleted
        }
        else{
            write(np,response_msg,sizeof(struct Message));

            if (msg.tskres == -1){
                stdout_from_fifo(&msg, "2LATE");
            }
            else stdout_from_fifo(&msg, "TSKDN");
		}
        free(response_msg);
        free(private_fifo_name);
   }
}

void* handle_request(void* arg){

    struct producer_thread_param* param = (struct producer_thread_param *) arg;

    struct Message msg = *param->msg;
    //free(param->msg);

    //check if there hasnt been a timeout
    double seconds_elapsed = time(NULL) - param->begin;
	if(seconds_elapsed >= param->seconds_to_run){
        msg.tskres = -1;
    }else{
		int server_response = task(msg.tskload);
		msg.tskres = server_response;
        stdout_from_fifo(&msg, "TSKEX");
    }


	sem_wait(&sem_full); //decreases one every time it adds to the buffer, when it reaches 0 the buffer is full so it blocks
    
    buffer[buffer_index] = msg;
    buffer_index++;
    sem_post(&sem_empty); //increses every time it adds to the buffer, signaling that there is something in the buffer and that the consumer can work

	pthread_exit(NULL);
}

void *thread_create(void* arg){

    struct thread_param *param = (struct thread_param *) arg;
    pthread_t ids[1000];
    size_t num_of_threads = 0;
 
    double seconds_elapsed = 0;

    //open the public fifo

    int public_fifo_descriptor = open(param->fifoname, O_RDONLY);

	
    struct producer_thread_param param_producer;
	while(seconds_elapsed < param->seconds_to_run){
        //check if theres requests on the public fifo, if so create a Producer thread
        struct Message *msg = malloc(sizeof(struct Message));
       
        int res = read(public_fifo_descriptor, msg, sizeof(struct Message));
        if(res > 0){
            param_producer.begin = param->begin;
            param_producer.msg = msg;
            param_producer.seconds_to_run = param->seconds_to_run;
            //create a producer thread
            stdout_from_fifo(msg, "RECVD");
			pthread_create(&ids[num_of_threads], NULL, handle_request, &param_producer);
			num_of_threads++;
            
		}
		seconds_elapsed = time(NULL) - param->begin;
    }
    close(public_fifo_descriptor);


	for(int i = 0; i< num_of_threads; i++) {
	    pthread_join(ids[i], NULL);	
	}

    pthread_exit(NULL);
}

int main(int argc, char* argv[]){
    time_t begin = time(NULL);

    char* fifoname = (char *) malloc(50 * sizeof(char));
    sprintf(fifoname,"%s",argv[5]);

    if (mkfifo(fifoname,0666) < 0) {
        perror("mkfifo");
    }
    
    //argv[1] should be "-t"
    //argv[2] should be an int
    //argv[3] should be "-l"
    //argv[4] should be an int
    if(argc != 6){
        printf("Invalid number of arguments! Usage: './s -t <no. of seconds> -t <buffersize> <public fifoname>' \n");
        return 0;
    }
    if(strcmp(argv[1],"-t") != 0){
        printf("Missing -t flag. Usage: './s -t <no. of seconds> -l <buffersize> <public fifoname>' \n");
        return 0;
    }

     if(strcmp(argv[3],"-l") != 0){
        printf("Missing -l flag. Usage: './s -t <no. of seconds> -l <buffersize> <public fifoname>' \n");
        return 0;
    }

    int seconds_to_run = atoi(argv[2]);
    buffer_size = atoi(argv[4]);

    if(seconds_to_run <= 0){
        printf("Time should be an integer greater than 0. Usage: './s -t <no. of seconds> -l <buffersize> <public fifoname>' \n");
        return 0;
    }

    if(buffer_size <= 0){
        printf("Buffer size should be an integr greater than 0. Usage: './s -t <no. of seconds> -l <buffersize> <public fifoname>' \n");
        return 0;
    }


	buffer = (struct Message*) malloc(buffer_size * sizeof(struct Message));


	sem_init(&sem_full, 0, buffer_size);

    sem_init(&sem_empty, 0, 0);

	pthread_t s0, sc;
    


	//create the consumer thread (envia mensagens do buffer para as threads privadas)
         
    struct thread_param param;
    param.begin = begin;
    param.seconds_to_run = seconds_to_run;
    param.fifoname = fifoname;

	pthread_create(&s0, NULL, thread_create, &param);
    pthread_create(&sc, NULL, thread_consumer,&param);
   
    pthread_join(s0,NULL);
    //buffer isn't empty so the consumer thread has to keep running
    while(buffer_index != 0){
    }

	pthread_cancel(sc); //If thread blocks because buffer is empty and is unable to notice that time is up.


	free(fifoname);
    free(buffer);

    //destroy semaphores
    sem_destroy(&sem_full);
	sem_destroy(&sem_empty);

    remove(fifoname);
	return 0;
}

