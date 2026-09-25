#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <unistd.h>

#define SHMSZ 10000000
typedef uint8_t  u8;


u8 initMap[100000];
u8* runtimeTogglePtr = initMap;
int shmid;

#define SHM_TOGGLE "__SHM_TOGGLE"

/* ydy - afl_toggle_shm_init */


/* ydy - toggleFuzzer Implementation */

/* ydy - Get rid of shared memory (atexit handler). */
// static void remove_shm_toggle(void) {

//   shmctl(shmid, IPC_RMID, NULL);

// }


void remove_shm_toggle(void) {

  shmctl(shmid, IPC_RMID, NULL);

}

void afl_toggle_shm_init() {

  u8* shm_str;
    // char* shm_str;
    shmid = shmget(IPC_PRIVATE, SHMSZ, IPC_CREAT | IPC_EXCL | 0600);

  if(shmid < 0) exit(1);
  atexit(remove_shm_toggle);
//   shm_str = alloc_printf("%d", shmid);
  if(asprintf(&shm_str, "%d", shmid) == -1 ) {
    exit(1);
  }   
  
  setenv(SHM_TOGGLE, shm_str, 1);

  free(shm_str);

  runtimeTogglePtr = shmat(shmid, NULL, 0);

  if(runtimeTogglePtr == (void*)-1) exit(1);
  // runtimeTogglePtr[1] = 1;


}

int totalCount = 0;
int **data = NULL;
int rows = 0;
int *rowSizes = NULL;

int cur_target_index = 0;

void initializeToggleInfo() {
  //   FILE *BBIndexCount = fopen("TotalBBIndexCount.txt", "r");
  // fscanf(BBIndexCount, "%d", &totalCount);
  // fclose(BBIndexCount);
  
  FILE *file = fopen("ToggleInfoCheck.txt", "r");

  char line[1000000];


  while (fgets(line, sizeof(line), file)) {
    int *row = NULL;
    int size = 0;
    char *token = strtok(line, " :[]\n");
    while (token != NULL) {
      token = strtok(NULL, " :[]\n");
      if(!token) continue;
        row = realloc(row, (size + 1) * sizeof(int));

        row[size++] = atoi(token);
    }

    data = realloc(data, (rows + 1) * sizeof(int *));

    rowSizes = realloc(rowSizes, (rows + 1) * sizeof(int));

    rowSizes[rows] = size;

    data[rows++] = row;

  }
  fclose(file);


}

void reset_toggle_shm() {
  for(int i = 0; i < 500; i++) {
    runtimeTogglePtr[i] = 1;
  }
}

void update_targets(int target) {
  cur_target_index = target;
  int rSizes = rowSizes[cur_target_index];
  for(int i = 0; i < rSizes; i++) {
    int toggleIndex = data[cur_target_index][i];
    runtimeTogglePtr[toggleIndex] = 0;
  }

}



int main() {
    initializeToggleInfo();
    afl_toggle_shm_init();
    reset_toggle_shm();
    update_targets(0);
    
    

    // char target_input[100];
    // fgets(target_input, sizeof(target_input), stdin);
    // target_input[strcspn(target_input, "\n")] = 0;
    char* binaryPath = "./toggle-output"; 
    // char* args[4]; 
    // args[0] = binaryPath; 
    // args[1] = strdup(target_input); 
    // args[2] = "additionalArgument"; 
    // args[3] = NULL; 

    pid_t pid = fork(); 

    if (pid == -1) {
        perror("fork failed");
        exit(1);
    } else if (pid == 0) {
        // char *binaryPath = "./output"; 
        // char *args[] = {binaryPath, strdup(target_input), NULL}; 

        execvp(binaryPath, NULL); 
        
        // perror("execvp failed");
        // exit(1); 
    } else {
        int status;
        printf("Wait for PID: %d\n", pid);
        sleep(1);
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            printf("Child exited with status %d\n", WEXITSTATUS(status));
        }
    }

    // remove_shm_toggle();
    return 0;

}

