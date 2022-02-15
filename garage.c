#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>

#define TOK_DELIM "\t\r\n\a"

typedef enum {
	false, true
} boolean;
typedef struct resource {
	int res_id;
	char* res_name;
	int amount;
} resource;

typedef struct service {
	int serv_id;
	char* serv_name;
	int time;
	int res_amount;
	int* res_id;
} service;

typedef struct request {
	long license_num;
	int arrival_time;
	int serv_amount;
	int* serv_id;
} request;

typedef struct GarageManager {
	resource* resources;
	service* services;
	request* requests;
	char* res_file;
	char* serv_file;
	char* req_file;
	int totalResources;
	int totalServices;
	int totalRequests;
} GManager, * P_GManager;

/*Data related functions, read and free*/
/*-------------------------------*/
boolean readResources();
boolean readServices();
boolean readRequests();
int calcFileSize(int);
void freeResources(resource*);
void freeServices(service*);
void freeRequests(request*);
void freeGarage();
/*-------------------------------*/

/*Sort requests by arrival time*/
/*-------------------------------*/
void sortRequests(request*);
void sortServices();
/*-------------------------------*/

/*Managing the time of day, each second reflected as 1 hour.*/
/*-------------------------------*/
void* manageTime();
/*-------------------------------*/

/*Requests managing*/
/*-------------------------------*/
void* requestsThreads();
void* startRequest(void*);
int findIndex(int);
int findIndex2(int);
/*-------------------------------*/

unsigned myClock = 0;
sem_t* sem_resources;
int requestsRemaining = 0;
P_GManager garage;

int main(int argc, char* argv[]) {
	boolean status;
	pthread_t clock_thread, requests_threads;
	int i;
	if (argc != 4) {
		perror("Bad input\n");
		exit(EXIT_FAILURE);
	}
	if (sem_init(&mutex, 0, 1) == -1) {
		perror("Can't init mutex\n");
		exit(EXIT_FAILURE);
	}
	garage = (GManager*)malloc(sizeof(GManager));
	if (!garage) {
		perror("Can't create garage");
		exit(EXIT_FAILURE);
	}
	garage->resources = NULL;
	garage->services = NULL;
	garage->requests = NULL;
	garage->totalRequests = 0;
	garage->totalResources = 0;
	garage->totalServices = 0;
	garage->res_file = (char*)malloc(sizeof(char) * (strlen(argv[1]) + 1));
	if (!garage->res_file) {
		perror("Failed to allocation memory\n");
		exit(EXIT_FAILURE);
	}
	strcpy(garage->res_file, argv[1]);
	garage->serv_file = (char*)malloc(sizeof(char) * (strlen(argv[2]) + 1));
	if (!garage->serv_file) {
		perror("Failed to allocation memory\n");
		exit(EXIT_FAILURE);
	}
	strcpy(garage->serv_file, argv[2]);
	garage->req_file = (char*)malloc(sizeof(char) * (strlen(argv[3]) + 1));
	if (!garage->req_file) {
		perror("Failed to allocation memory\n");
		exit(EXIT_FAILURE);
	}
	strcpy(garage->req_file, argv[3]);
	/*Getting data from the files*/
	/*-------------------------------*/
		/*Reading resources:*/
	status = readResources();
	switch (status) {
	case false:
		freeResources(garage->resources);
		perror("Resources reading failed\n");
		free(garage);
		exit(EXIT_FAILURE);
	case true:
		/*Initializing semaphore for each resource with the amount of that specific resource.*/
		sem_resources = (sem_t*)malloc(sizeof(sem_t) * garage->totalResources);
		if (!sem_resources) {
			freeResources(garage->resources);
			free(garage);
			perror("Failed to create resource's semaphore\n");
			exit(EXIT_FAILURE);
		}
		for (i = 0; i < garage->totalResources; i++) {
			if (sem_init(&sem_resources[i], 0, garage->resources[i].amount)
				== -1) {
				perror("Can't create semaphore for resources\n");
				freeResources(garage->resources);
				freeGarage();
				exit(1);
			}
		}
		break;
	}

	/*-------------------------------*/
		/*Reading services*/
	status = readServices();
	if (status == false) {
		freeResources(garage->resources);
		freeServices(garage->services);
		free(sem_resources);
		freeGarage();
		perror("Services reading failed\n");
		exit(EXIT_FAILURE);
	}
	sortServices();
	/*-------------------------------*/
		/*Reading requests*/
	status = readRequests();
	switch (status) {
	case false:
		freeResources(garage->resources);
		freeServices(garage->services);
		freeRequests(garage->requests);
		free(sem_resources);
		freeGarage();
		perror("Requests reading failed\n");
		exit(EXIT_FAILURE);
	case true:
		sortRequests(garage->requests);
	}

	/*-------------------------------*/
		/*Creating the clock.*/
	if (pthread_create(&clock_thread, NULL, manageTime, NULL) != 0) {
		freeResources(garage->resources);
		freeServices(garage->services);
		freeRequests(garage->requests);
		free(sem_resources);
		freeGarage();
		perror("Can't create clock's thread\n");
		exit(EXIT_FAILURE);
	}
	/*-------------------------------*/
		/*Starting a managing thread for requests.*/
	if (pthread_create(&requests_threads, NULL, requestsThreads, NULL) != 0) {
		freeResources(garage->resources);
		freeServices(garage->services);
		freeRequests(garage->requests);
		free(sem_resources);
		freeGarage();
		perror("Can't create requests thread\n");
		exit(EXIT_FAILURE);
	}
	/*Waiting for managing thread of requests to finish.*/
	if (pthread_join(requests_threads, NULL) != 0) {
		freeResources(garage->resources);
		freeServices(garage->services);
		freeRequests(garage->requests);
		free(sem_resources);
		freeGarage();
		perror("Failed to join function for the thread.\n");
		exit(EXIT_FAILURE);
	}
	freeResources(garage->resources);
	freeServices(garage->services);
	freeRequests(garage->requests);
	freeGarage();
	free(sem_resources);
	return 0;
}

/*Function to calculate the size of the file.*/
int calcFileSize(int file_fd) {
	int count = 0, rbytes;
	char buff[256];
	if ((rbytes = read(file_fd, buff, 256)) == -1) {
		return -1;
	}
	count += rbytes;
	while (rbytes > 0) {
		if ((rbytes = read(file_fd, buff, 256)) == -1) {
			return -1;
		}
		count += rbytes;
	}
	return count;
}

/*Reading resources from file
 *input: array of pointers to resources structure.*/
boolean readResources() {
	int resource_fd, bytes;
	resource* temp = NULL;
	char* buff, * token;
	resource_fd = open(garage->res_file, O_RDONLY);
	if (resource_fd == -1) {
		return false;
	}
	bytes = calcFileSize(resource_fd);
	close(resource_fd);
	if (bytes == -1) {
		return false;
	}
	resource_fd = open(garage->res_file, O_RDONLY);
	if (resource_fd == -1) {
		return false;
	}
	buff = (char*)malloc(sizeof(char) * bytes);
	if (!buff) {
		return false;
	}
	if (read(resource_fd, buff, bytes) == -1) {
		free(buff);
		return false;
	}
	close(resource_fd);
	token = strtok(buff, TOK_DELIM);
	/*Breaking the buffer to get the data for each resource.*/
	while (token != NULL) {
		temp = realloc(temp, sizeof(resource) * (garage->totalResources + 1));
		if (!temp) {
			free(buff);
			return false;
		}
		temp[garage->totalResources].res_id = atoi(token);
		token = strtok(NULL, TOK_DELIM);
		temp[garage->totalResources].res_name = (char*)malloc(
			sizeof(char) * (strlen(token) + 1));
		if (!temp[garage->totalResources].res_name) {
			free(buff);
			return false;
		}
		strcpy(temp[garage->totalResources].res_name, token);
		token = strtok(NULL, TOK_DELIM);
		temp[garage->totalResources].amount = atoi(token);
		garage->totalResources++; //Counting the total amount of resources to create semaphores.
		token = strtok(NULL, TOK_DELIM);
	}
	free(buff);
	garage->resources = temp;
	return true;
}

/*Reading services from file
 *input: array of pointers to services structure.*/
boolean readServices() {
	int service_fd, bytes, i;
	char* buff, * token;
	service* temp = NULL;
	service_fd = open(garage->serv_file, O_RDONLY);
	if (service_fd == -1) {
		return false;
	}
	bytes = calcFileSize(service_fd);
	close(service_fd);
	if (bytes == -1) {
		return false;
	}
	service_fd = open(garage->serv_file, O_RDONLY);
	if (service_fd == -1) {
		return false;
	}
	buff = (char*)malloc(sizeof(char) * bytes);
	if (!buff) {
		return false;
	}
	if (read(service_fd, buff, bytes) == -1) {
		free(buff);
		return false;
	}
	close(service_fd);
	token = strtok(buff, TOK_DELIM);
	/*Breaking the buffer to get the data for each service.*/
	while (token != NULL) {
		temp = realloc(temp, sizeof(service) * (garage->totalServices + 1));
		if (!temp) {
			free(buff);
			return false;
		}
		temp[garage->totalServices].serv_id = atoi(token);
		token = strtok(NULL, TOK_DELIM);
		temp[garage->totalServices].serv_name = (char*)malloc(
			sizeof(char) * (strlen(token) + 1));
		if (!temp[garage->totalServices].serv_name) {
			free(buff);
			return false;
		}
		strcpy(temp[garage->totalServices].serv_name, token);
		token = strtok(NULL, TOK_DELIM);
		temp[garage->totalServices].time = atoi(token);
		token = strtok(NULL, TOK_DELIM);
		temp[garage->totalServices].res_amount = atoi(token);
		temp[garage->totalServices].res_id = (int*)malloc(
			sizeof(int) * temp[garage->totalServices].res_amount);
		if (!temp[garage->totalServices].res_id) {
			free(buff);
			return false;
		}
		for (i = 0; i < temp[garage->totalServices].res_amount; i++) {
			token = strtok(NULL, TOK_DELIM);
			temp[garage->totalServices].res_id[i] = atoi(token);
		}
		garage->totalServices++;//Counting the total amount of services
		token = strtok(NULL, TOK_DELIM);
	}
	free(buff);
	garage->services = temp;
	return true;
}

/*Reading requests from file
 *input: array of pointers to requests structure.*/
boolean readRequests() {
	int requests_fd, bytes, i;
	char* buff, * token;
	request* temp = NULL;
	requests_fd = open(garage->req_file, O_RDONLY);
	if (requests_fd == -1) {
		return false;
	}
	bytes = calcFileSize(requests_fd);
	close(requests_fd);
	if (bytes == -1) {
		return false;
	}
	requests_fd = open(garage->req_file, O_RDONLY);
	if (requests_fd == -1) {
		return false;
	}
	buff = (char*)malloc(sizeof(char) * bytes);
	if (!buff) {
		return false;
	}
	if (read(requests_fd, buff, bytes) == -1) {
		free(buff);
		return false;
	}
	close(requests_fd);
	token = strtok(buff, TOK_DELIM);
	/*Breaking the buffer to get the data for each service.*/
	while (token != NULL) {
		temp = realloc(temp, sizeof(request) * (garage->totalRequests + 1));
		if (!temp) {
			free(buff);
			return false;
		}
		temp[garage->totalRequests].license_num = atoi(token);
		token = strtok(NULL, TOK_DELIM);
		temp[garage->totalRequests].arrival_time = atoi(token);
		token = strtok(NULL, TOK_DELIM);
		temp[garage->totalRequests].serv_amount = atoi(token);
		temp[garage->totalRequests].serv_id = (int*)malloc(
			sizeof(int) * temp[garage->totalRequests].serv_amount);
		if (!temp[garage->totalRequests].serv_id) {
			free(buff);
			return false;
		}
		for (i = 0; i < temp[garage->totalRequests].serv_amount; i++) {
			token = strtok(NULL, TOK_DELIM);
			temp[garage->totalRequests].serv_id[i] = atoi(token);
		}
		garage->totalRequests++;//Counting the total requests.
		token = strtok(NULL, TOK_DELIM);
	}
	free(buff);
	garage->requests = temp;
	return true;
}

/*Free all the memory allocated for the resources.
 * input: array of resources.*/
void freeResources(resource* resources) {
	int i;
	for (i = 0; i < garage->totalResources; i++) {
		free(resources[i].res_name);
	}
	free(resources);
}
/*Free all the memory allocated for the services.
 * input: array of services.*/
void freeServices(service* services) {
	int i;
	for (i = 0; i < garage->totalResources; i++) {
		free(services[i].res_id);
	}
	free(services);
}
/*Free all the memory allocated for the requests.
 * input: array of requests.*/
void freeRequests(request* requests) {
	int i;
	for (i = 0; i < garage->totalRequests; i++) {
		free(requests[i].serv_id);
	}
	free(requests);
}

/*Free all memory allocated to the garage.*/
void freeGarage() {
	free(garage->res_file);
	free(garage->serv_file);
	free(garage->req_file);
	free(garage);
}



/*Sorting the requests by their arrival time with insertion sort.*/
void sortRequests(request* requests) {
	int i, j;
	request key;
	for (i = 1; i < garage->totalRequests; i++) {
		key = requests[i];
		j = i - 1;
		while (j >= 0 && key.arrival_time < requests[j].arrival_time) {
			requests[j + 1] = requests[j];
			j--;
		}
		requests[j + 1] = key;
	}
}

/*Sorting for each service their resources to prevent deadlock.*/
void sortServices() {
	int i, j, k, key;
	for (i = 0; i < garage->totalServices; i++) {
		for (j = 1; j < garage->services[i].res_amount; j++) {
			key = garage->services[i].res_id[j];
			k = j - 1;
			while (k >= 0 && key < garage->services[i].res_id[k]) {
				garage->services[i].res_id[k + 1] = garage->services[i].res_id[k];
				k--;
			}
			garage->services[i].res_id[k + 1] = key;
		}
	}
}

/*Function for the clock's thread,
 * simulating the time, each second represents 1 hour.*/
void* manageTime() {
	while (1) {
		sleep(1);
		myClock = (myClock + 1) % 24;
	}
	pthread_exit(NULL);
}

/*Function for the managing thread.
 * Allocating memory for threads as big as the total requests,
 * so each request is handled in its own thread.*/
void* requestsThreads() {
	int i = 0;
	pthread_t* req_threads = (pthread_t*)malloc(
		sizeof(pthread_t) * garage->totalRequests);
	if (!req_threads) {
		pthread_exit((void*)(-1));
	}
	printf("Total requests: %d\n", garage->totalRequests);
	while (requestsRemaining < garage->totalRequests) {
		if (garage->requests[i].arrival_time == myClock) {
			printf("car: %ld time: %d has arrived.\n",
				garage->requests[i].license_num,
				garage->requests[i].arrival_time);
			if (pthread_create(&req_threads[i], NULL, startRequest,
				&(garage->requests[i])) != 0) {
				pthread_exit((void*)(-1));
			}
			i = (i + 1) % garage->totalRequests;
		}
	}
	printf("Finish all requests\n");
	pthread_exit(NULL);
}

/*Function for handling each request in a thread.*/
void* startRequest(void* requestInProsses) {
	request inProsses;
	inProsses = *((request*)requestInProsses);
	int i, j, serv_id, k, * res_index, req_index;
	req_index = findIndex2(inProsses.license_num);
	printf("car: %ld time: %d is up for discussion.\n", inProsses.license_num,
		myClock);
	for (i = 0; i < inProsses.serv_amount; i++) {
		serv_id = inProsses.serv_id[i];
		j = 0;
		while (j < garage->totalServices && garage->services[j].serv_id != serv_id) {
			j++;
		}
		res_index = (int*)malloc(sizeof(int) * garage->services[j].res_amount);
		if (!res_index) {
			pthread_exit((void*)(-1));
		}
		/*Getting index of each resource needed for the service,
		 * in order to alert that the service is using the resource.*/
		for (k = 0; k < garage->services[j].res_amount; k++) {
			res_index[k] = findIndex(garage->services[j].res_id[k]);
		}
		for (k = 0; k < garage->services[j].res_amount; k++) {
			sem_wait(&sem_resources[res_index[k]]);
		}
		printf("car: %ld time: %d started %s.\n", inProsses.license_num,
			myClock, garage->services[j].serv_name);
		sleep(garage->services[j].time);
		for (k = 0; k < garage->services[j].res_amount; k++) {
			sem_post(&sem_resources[res_index[k]]);
		}
		free(res_index);
		printf("car: %ld time: %d completed %s.\n", inProsses.license_num,
			myClock, garage->services[j].serv_name);
	}
	printf("car: %ld time: %d service complete.\n", inProsses.license_num,
		myClock);
	garage->requests[req_index].arrival_time = -1;
	requestsRemaining++;
	pthread_exit(NULL);
}


int findIndex(int res_id) {
	int i;
	for (i = 0; i < garage->totalResources; i++) {
		if (garage->resources[i].res_id == res_id) {
			return i;
		}
	}
	return -1;
}
int findIndex2(int license) {
	int i;
	for (i = 0; i < garage->totalRequests; i++) {
		if (garage->requests[i].license_num == license) {
			return i;
		}
	}
	return -1;
}
