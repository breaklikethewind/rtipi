

typedef enum {
	TYPE_NULL,
	TYPE_INTEGER,
	TYPE_FLOAT,
	TYPE_STRING
} data_type_e;

typedef int (*cmdfunc)(char* request, char* response);

typedef struct 
{
	char request[20];
	char tag[20];
	cmdfunc commandfunc;
	data_type_e data_type;
	void* data;
} commandlist_t;

typedef struct 
{
	char tag[20];
	data_type_e data_type;
	void* data;
} pushlist_t;

int tp_handle_requests(commandlist_t* device_commandlist, pthread_mutex_t* lock);
int tp_handle_data_push(pushlist_t* pushdata, pthread_mutex_t* lock);
void tp_stop_handlers(void);
void tp_force_data_push(void);


