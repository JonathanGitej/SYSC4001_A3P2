# SYSC 4001 A3 P2

### Authors
- Jonathan Gitej (101294584)
- Atik Mahmud (101318070)

## Description
This program simulates multiple TAs marking exams concurrently where part 2a does not involve synchronization whilist part 2 b involves synchronization through the use of semaphores.

- Each TA process can mark questions on exams and modify a shared rubric.  
- Shared memory is used to store exams, student IDs, and the rubric.  
- **SysV semaphores** are used to synchronize access to the rubric and prevent simultaneous modifications.  
- The program supports **2 or more TAs** running concurrently.

## Compiling
Use `gcc` to compile the program:

### Part 2a
- First Compile all files in directory:
```bash
gcc *.c -o part2a_101294584_101318070
```
- Secondly make sure rubric.txt and the exams directory is in the root with the c file
- Lastly to run:
```bash
./part2a_101294584_101318070 <num_TAs> <exam_directory>
```

num_TAs can be any integer >= 2 (default value is 2)
exam_directory is the directory where your exam.txt are located (in this case ./exams)

- example execution:
```bash
gcc *.c -o part2a_101294584_101318070
./part2a_101294584_101318070 2 ./exams
```

  ### Part 2b
- First Compile all files in directory:
```bash
gcc *.c -o part2b_101294584_101318070
```
- Secondly make sure rubric.txt and the exams directory is in the root with the c file
- Lastly to run:
```bash
./part2b_101294584_101318070 <num_TAs> <exam_directory>
```

num_TAs can be any integer >= 2 (default value is 2)
exam_directory is the directory where your exam.txt are located (in this case ./exams)

- example execution:
```bash
gcc *.c -o part2b_101294584_101318070
./part2b_101294584_101318070 2 ./exams
```

  
  
