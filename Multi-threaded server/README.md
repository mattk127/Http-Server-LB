# README

### Instructions  
1. Complie the Program using the command *make*, in the directory which contains the Makefile.
    ```sh
    $ make
    ```
2. run the program with a specified port number, and optional arguments `-l` & `-N` to include logging and specify the number of threads you want to the program to run on.
    ```sh
    $ ./httpserver 8080 -l logFile -N 4
    ```
    It is unclear if the multithreading works, as well as the logging, as the basic PUT and GET requests have issues with binary files. 
    Healthchecking is also not implemented in this program.