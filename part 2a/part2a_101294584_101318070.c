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

#define RUBRIC_LINES 5
#define MAX_LINES 200
#define MAX_EXAMS 100

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
        if (isdigit(*filename)) {
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
void load_exam(shared_data *shm, int index)
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
void check_rubric(shared_data *shm, int ta_id)
{
    printf("TA %d checking rubric...\n", ta_id);

    for (int i = 0; i < RUBRIC_LINES; i++) {

        usleep(500000 + rand() % 500000);  //delay

        int change = rand() % 100 < 30;    // 30% chance to change

        if (change) {

            // Find the comma separating exercise number and rubric letter
            char *comma = strchr(shm->rubric[i], ',');
            if (comma && comma[1] != '\0') {

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

            } else {
                printf("TA %d could not modify rubric line %d (invalid format)\n",
                       ta_id, i + 1);
            }
        }
    }
}

//Mark the question
void mark_questions(shared_data *shm, int ta_id)
{
    for (int i = 0; i < RUBRIC_LINES; i++) {

        if (shm->questions_marked[i] == 0) {

            printf("TA %d marking Q%d for student %d\n",
                   ta_id, i + 1, shm->student_id);

            usleep(1000000 + rand() % 1000000);

            shm->questions_marked[i] = 1;

            printf("TA %d finished Q%d for student %d\n",
                   ta_id, i + 1, shm->student_id);
        }
    }
}

//TA logic
void ta_process(shared_data *shm, int ta_id)
{
    srand(time(NULL) ^ getpid());

    while (!shm->finished_exams)
    {
        check_rubric(shm, ta_id);
        mark_questions(shm, ta_id);

        // check if all questions done
        int all_done = 1;
        for (int i = 0; i < RUBRIC_LINES; i++) {
            if (shm->questions_marked[i] == 0)
                all_done = 0;
        }

        if (all_done) {
            shm->exam_index++;

            if (shm->exam_index < shm->total_exams) {
                load_exam(shm, shm->exam_index);
            } else {
                shm->finished_exams = 1;
            }
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
    key_t key = 1111;
    int shmid = shmget(key, sizeof(shared_data), 0666 | IPC_CREAT);
    if (shmid == -1) { perror("shmget"); exit(1); }

    shared_data *shm = (shared_data *)shmat(shmid, NULL, 0);

    // zero shared memory to avoid garbage
    memset(shm, 0, sizeof(shared_data));

    load_rubric(shm);

    // scan exams and sort
    shm->total_exams = scan_exam_directory(shm, exam_dir);
    shm->exam_index = 0;
    shm->finished_exams = 0;

    load_exam(shm, 0);

    // Fork TAs
    for (int i = 0; i < num_tas; i++) {
        if (fork() == 0) {
            ta_process(shm, i + 1);
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

    printf("All exams finished.\n");
    return 0;
}
