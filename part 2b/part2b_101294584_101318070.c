#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/sem.h>
#include <errno.h>

#define RUBRIC_LINES 5
#define MAX_LINES 200
#define MAX_EXAMS 100

// semaphore indices
#define SEM_RUBRIC 0      // protect rubric
#define SEM_EXAMIDX 1     // protect exam_index and load_exam
#define SEM_QUESTIONS 2   // protect questions_marked

#define NUM_SEMS 3

// Shared memory structure
typedef struct 
{
    char rubric[RUBRIC_LINES][MAX_LINES];
    char current_exam[MAX_LINES];
    int questions_marked[RUBRIC_LINES];
    int student_id;
    int total_exams;
    int finished_exams;
    int exam_index;
    char exam_list[MAX_EXAMS][MAX_LINES]; // store file names
} shared_data;

// Helper sem operations
void sem_op(int semid, int semnum, int op) {
    struct sembuf s;
    s.sem_num = semnum;
    s.sem_op = op;
    s.sem_flg = 0;
    if (semop(semid, &s, 1) == -1) {
        perror("semop");
        exit(1);
    }
}

void sem_down(int semid, int semnum) { sem_op(semid, semnum, -1); }
void sem_up(int semid, int semnum)   { sem_op(semid, semnum, 1);  }

//Load rubric.txt
void load_rubric(shared_data *shm)
{
    FILE *f = fopen("rubric.txt", "r");
    if (!f) {
        perror("rubric.txt open failed");
        exit(1);
    }

    for (int i = 0; i < RUBRIC_LINES; i++) {
        if (!fgets(shm->rubric[i], MAX_LINES, f)) {
            strcpy(shm->rubric[i], "EMPTY");
        }
        shm->rubric[i][strcspn(shm->rubric[i], "\n")] = 0;
    }
    fclose(f);
}

// Save current rubric in shared memory back to rubric.txt
void save_rubric_to_file(shared_data *shm)
{
    FILE *f = fopen("rubric.txt", "w");
    if (!f) {
        perror("Failed to open rubric.txt for writing");
        return;
    }
    for (int i = 0; i < RUBRIC_LINES; i++) {
        fprintf(f, "%s\n", shm->rubric[i]);
    }
    fclose(f);
}

//used to sort exams in numerical order (extract number from filename)
int extract_number(const char *filename) {
    int num = 0;
    while (*filename) {
        if (isdigit((unsigned char)*filename)) {
            num = num * 10 + (*filename - '0');
        }
        filename++;
    }
    return num;
}

//adds exams to exam_list from directory
int scan_exam_directory(shared_data *shm, const char *dirpath)
{
    DIR *d = opendir(dirpath);
    if (!d) {
        perror("Failed to open exam directory");
        exit(1);
    }

    struct dirent *file;
    int count = 0;

    while ((file = readdir(d)) != NULL) {
        if (file->d_name[0] == '.') continue;
        if (strstr(file->d_name, ".txt")) {
            snprintf(shm->exam_list[count],
                     MAX_LINES,
                     "%s/%s",
                     dirpath,
                     file->d_name);
            count++;
            if (count >= MAX_EXAMS) break;
        }
    }

    closedir(d);

    //Sort exams numerically
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            int num_i = extract_number(shm->exam_list[i]);
            int num_j = extract_number(shm->exam_list[j]);

            if (num_j < num_i) {
                char temp[MAX_LINES];
                strcpy(temp, shm->exam_list[i]);
                strcpy(shm->exam_list[i], shm->exam_list[j]);
                strcpy(shm->exam_list[j], temp);
            }
        }
    }

    return count;
}

//load an exam into shared memory
void load_exam(shared_data *shm, int index, int semid)
{
    if (index < 0 || index >= shm->total_exams) return;
    
    FILE *f = fopen(shm->exam_list[index], "r");
    if (!f) {
        perror("Exam file open failed");
        exit(1);
    }

    if (!fgets(shm->current_exam, MAX_LINES, f)) {
        perror("Failed to read exam file");
        fclose(f);
        exit(1);
    }

    if (shm->student_id == 9999) {
        shm->finished_exams = 1;
        return;
    }

    shm->current_exam[strcspn(shm->current_exam, "\n")] = 0;
    shm->student_id = atoi(shm->current_exam);

    fclose(f);

    for (int i = 0; i < RUBRIC_LINES; i++)
        shm->questions_marked[i] = 0;

    printf("Loaded exam %d: %s (Student %d)\n",
           index + 1,
           shm->exam_list[index],
           shm->student_id);
}

//TA's randomly modify rubric
void check_rubric(shared_data *shm, int ta_id, int semid)
{
    printf("TA %d checking rubric...\n", ta_id);

    for (int i = 0; i < RUBRIC_LINES; i++) {

        usleep(500000 + rand() % 500000);  //delay

        int change = rand() % 100 < 30;    // 30% chance to change

        if (change) {

            // Find the comma separating exercise number and rubric letter
            char *comma = strchr(shm->rubric[i], ',');
            if (comma && comma[1] != '\0') {

                // Acquire rubric write lock
                sem_down(semid, SEM_RUBRIC);

                char old_val = comma[1];
                char new_val;

                // Only cycle between 'A' and 'Z'
                if (old_val >= 'A' && old_val < 'Z') {
                    new_val = old_val + 1;
                } else {
                    new_val = 'A';
                }

                comma[1] = new_val;

                printf("TA %d corrected rubric line %d: '%c' -> '%c'\n",
                       ta_id, i + 1, old_val, new_val);

                save_rubric_to_file(shm);
                
                // release lock
                sem_up(semid, SEM_RUBRIC);

            } else {
                printf("TA %d could not modify rubric line %d (invalid format)\n",
                       ta_id, i + 1);
            }
        }
    }
}

//Mark the question
void mark_questions(shared_data *shm, int ta_id, int semid)
{
    while (1) {
        int pick = -1;

        // Find and reserve a free question (0 -> 2)
        sem_down(semid, SEM_QUESTIONS);
        for (int i = 0; i < RUBRIC_LINES; i++) {
            if (shm->questions_marked[i] == 0) {
                shm->questions_marked[i] = 2; // question is in progress
                pick = i;
                break;
            }
        }
        sem_up(semid, SEM_QUESTIONS);

        if (pick == -1) {
            // no free questions left
            return;
        }

        printf("TA %d marking Q%d for student %d\n",
               ta_id, pick + 1, shm->student_id);

        usleep(1000000 + rand() % 1000000);

        // Mark as finished
        sem_down(semid, SEM_QUESTIONS);
        shm->questions_marked[pick] = 1;
        sem_up(semid, SEM_QUESTIONS);

        printf("TA %d finished Q%d for student %d\n",
               ta_id, pick + 1, shm->student_id);

        // continue to try to pick another question until none left
    }
}

//TA logic
void ta_process(shared_data *shm, int ta_id, int semid)
{
    srand(time(NULL) ^ getpid());

    while (!shm->finished_exams)
    {

        check_rubric(shm, ta_id, semid);
        mark_questions(shm, ta_id, semid);

        // check if all questions done
        int all_done = 1;
        sem_down(semid, SEM_QUESTIONS);
        for (int i = 0; i < RUBRIC_LINES; i++) {
            if (shm->questions_marked[i] == 0 || shm->questions_marked[i] == 2) {
                // if any free or in-progress exist, not all done
                if (shm->questions_marked[i] == 0) all_done = 0;
                else if (shm->questions_marked[i] == 2) all_done = 0;
            }
            if (shm->questions_marked[i] == 0) { all_done = 0; break; }
            if (shm->questions_marked[i] == 2) { all_done = 0; break; }
        }
        sem_up(semid, SEM_QUESTIONS);

        if (all_done) {
            // Only one TA should advance/load the next exam
            sem_down(semid, SEM_EXAMIDX);

            int idx = shm->exam_index;
            int any_free = 0;
            for (int i = 0; i < RUBRIC_LINES; i++) {
                if (shm->questions_marked[i] == 0 || shm->questions_marked[i] == 2) {
                    any_free = 1; break;
                }
            }

            if (!any_free) {
                // advance index and load next
                shm->exam_index++;
                if (shm->exam_index < shm->total_exams) {
                    load_exam(shm, shm->exam_index, semid);
                } else {
                    shm->finished_exams = 1;
                }
            }

            sem_up(semid, SEM_EXAMIDX);
        }

        usleep(100000);
    }

    printf("TA %d exiting.\n", ta_id);
}

int main(int argc, char *argv[])
{
    int num_tas = 2;  // default number of TAs
    const char *exam_dir;

    if (argc == 2) {
        exam_dir = argv[1];
        printf("No TA count provided. Defaulting to %d TAs.\n", num_tas);
    }
    else if (argc == 3) {
        num_tas = atoi(argv[1]);
        exam_dir = argv[2];

        if (num_tas < 2) {
            printf("TA count must be at least 2. Setting num_tas = 2.\n");
            num_tas = 2;
        }
    }
    else {
        printf("Usage: %s <num_TAs> <exam_directory>\n", argv[0]);
        exit(1);
    }

    // create shared memory
    key_t shmkey = 1111;
    int shmid = shmget(shmkey, sizeof(shared_data), 0666 | IPC_CREAT);
    if (shmid == -1) { perror("shmget"); exit(1); }

    shared_data *shm = (shared_data *)shmat(shmid, NULL, 0);
    if (shm == (void*)-1) { perror("shmat"); exit(1); }

    // zero shared memory to avoid garbage
    memset(shm, 0, sizeof(shared_data));

    // create semaphores
    key_t semkey = 2222;
    int semid = semget(semkey, NUM_SEMS, IPC_CREAT | 0666);
    if (semid == -1) { perror("semget"); exit(1); }

    // initialize semaphores
    union semun arg;
    unsigned short vals[NUM_SEMS] = {1,1,1};
    arg.array = vals;
    if (semctl(semid, 0, SETALL, arg) == -1) {
        perror("semctl SETALL");
        // cleanup
        shmctl(shmid, IPC_RMID, NULL);
        exit(1);
    }

    load_rubric(shm);

    // scan exams and sort
    shm->total_exams = scan_exam_directory(shm, exam_dir);
    shm->exam_index = 0;
    shm->finished_exams = 0;

    // Load first exam under exam lock to be safe
    sem_down(semid, SEM_EXAMIDX);
    load_exam(shm, 0, semid);
    sem_up(semid, SEM_EXAMIDX);

    // Fork TAs
    for (int i = 0; i < num_tas; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            semctl(semid, 0, IPC_RMID);
            shmctl(shmid, IPC_RMID, NULL);
            exit(1);
        }
        if (pid == 0) {
            ta_process(shm, i + 1, semid);
            shmdt(shm);
            exit(0);
        }
    }

    for (int i = 0; i < num_tas; i++) {
        wait(NULL);
    }

    // cleanup
    shmdt(shm);
    shmctl(shmid, IPC_RMID, NULL);
    if (semctl(semid, 0, IPC_RMID) == -1) {
        perror("semctl IPC_RMID");
    }

    printf("All exams finished.\n");
    return 0;
}
