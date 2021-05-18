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
    pthread_mutex_lock(&mutex);
    for (int i = 0; i < buffer_size-1;i++){
        buffer[i] = buffer[i+1];
    }
    buffer_index--;
    pthread_mutex_unlock(&mutex);
}

void* thread_consumer(){
   
   struct Message msg;

   while(true){     
        char* private_fifo_name = (char *) malloc(50 * sizeof(char));
        if (private_fifo_name == NULL) perror("failed to allocate memory for the private FIFO's name!");

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
            if (write(np,response_msg,sizeof(struct Message)) == -1){
                stdout_from_fifo(&msg, "FAILD");
            }
            else{
                if (msg.tskres == -1){
                    stdout_from_fifo(&msg, "2LATE");
                }
                else stdout_from_fifo(&msg, "TSKDN");
            }
		}
        free(response_msg);
        free(private_fifo_name);
   }
}

void* handle_request(void* arg){

    struct producer_thread_param* param = (struct producer_thread_param *) arg;

    struct Message msg = *param->msg;

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
    pthread_mutex_lock(&mutex);
    buffer[buffer_index] = msg;
    buffer_index++;
    pthread_mutex_unlock(&mutex);
    sem_post(&sem_empty); //increses every time it adds to the buffer, signaling that there is something in the buffer and that the consumer can work

	pthread_exit(NULL);
}

void *thread_create(void* arg){

    struct thread_param *param = (struct thread_param *) arg;
    pthread_t ids[1000];
    struct Message *messages[1000];
    size_t num_of_threads = 0;
 
    double seconds_elapsed = 0;

    //open the public fifo

    int public_fifo_descriptor = open(param->fifoname, O_RDONLY);
	
    struct producer_thread_param param_producer;
	while(seconds_elapsed < param->seconds_to_run){
        struct Message *msg = malloc(sizeof(struct Message));
        if (msg == NULL) perror("failed to allocate memory for message");

        int res = read(public_fifo_descriptor, msg, sizeof(struct Message));
        //check if theres requests on the public fifo, if so create a Producer thread
        if(res > 0){
            param_producer.begin = param->begin;
            param_producer.msg = msg;
            param_producer.seconds_to_run = param->seconds_to_run;
            
            stdout_from_fifo(msg, "RECVD");

            //create a producer thread
			if (pthread_create(&ids[num_of_threads], NULL, handle_request, &param_producer) != 0) perror("Could not create thread!");
            messages[num_of_threads] = msg;
			num_of_threads++;
		}
		seconds_elapsed = time(NULL) - param->begin;
    }

    if (close(public_fifo_descriptor) == -1) perror("Could not close public FIFO!");

	for(int i = 0; i< num_of_threads; i++) {
	    if(pthread_join(ids[i], NULL) != 0) perror("Could not join thread!");
	}
    for (int i = 0; i< num_of_threads; i++){
        free(messages[i]);
    }

    pthread_exit(NULL);
}

int main(int argc, char* argv[]){
    time_t begin = time(NULL);

    char* fifoname = (char *) malloc(50 * sizeof(char));
    if (fifoname == NULL) perror("failed to allocate memory for fifoname");

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

    //allocating memory for the buffer
	buffer = (struct Message*) malloc(buffer_size * sizeof(struct Message));
    if (buffer == NULL) perror("failed to allocate memory for buffer");

	if (sem_init(&sem_full, 0, buffer_size) == -1) perror("failed to create semaphore");
    if (sem_init(&sem_empty, 0, 0) == -1) perror("failed to create semaphore");
    
	pthread_t s0, sc;
         
    struct thread_param param;
    param.begin = begin;
    param.seconds_to_run = seconds_to_run;
    param.fifoname = fifoname;

    //create the main thread (cria threads produtoras)
	if(pthread_create(&s0, NULL, thread_create, &param) != 0) perror("Could not create thread!");

    //create the consumer thread (envia mensagens do buffer para as threads privadas)
    if(pthread_create(&sc, NULL, thread_consumer, NULL) != 0) perror("Could not create thread!");
   
    if(pthread_join(s0,NULL) != 0) perror("Could not join thread!");

    //if buffer isn't empty the consumer thread has to keep running
    while(buffer_index != 0){
    }

    //We can cancel the consumer thread
	pthread_cancel(sc);

    //freeing memory
	free(fifoname);
    free(buffer);

    //destroy semaphores
    sem_destroy(&sem_full);
	sem_destroy(&sem_empty);

    //removing public FIFO
    remove(fifoname);

	return 0;
}

