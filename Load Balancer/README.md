# README

### Instructions  
1. Complie the Program using the command *make*, in the directory which contains the Makefile.
    ```sh
    $ make
    ```
2. run the program with a specified port number which the load balancer will receive connections on. Additional port numbers may be specified for instances of an httpserver to run on.  and optional arguments `-R` & `-N` to include the number of requests after which healthcheck will run again, and specify the number of concurrent requests you want the loadbalancer to handle at one time.
    ```sh
    $ ./loadbalancer 1234 -N 4 8080 8081 -R 10
    ```
    Continuous healthchecking is not endabled, but the LB is able to run an initial healthcheck.