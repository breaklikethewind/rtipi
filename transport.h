

typedef enum {
	TYPE_NULL,
	TYPE_INTEGER,
	TYPE_FLOAT,
	TYPE_STRING
} data_type_e;

typedef int (*cmdfunc)(char* request, char* response);

typedef struct commandlist
{
	char request[20];
	char tag[20];
	cmdfunc commandfunc;
	data_type_e data_type;
	void* data;
} commandlist_t;

typedef struct pushlist
{
	char tag[20];
	data_type_e data_type;
	void* data;
} pushlist_t;

void handle_requests(commandlist_t* device_commandlist, pthread_mutex_t* lock, int* exit);
void push_data(pushlist_t* pushdata, pthread_mutex_t* lock, int* exit);
void thread_exit(void);

