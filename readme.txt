For our Liso implementation, we first create a new socket with the socket() system call. Then, we bind the socket to an address using the bind() system call. Next, we listen for connections with the listen() system call. After we build the socket, we initialize the pool structure to manage a pool of connected cients. The last step is to use an infinite loop to handle all existing and upcoming clients. In the infinite loop, we use 'select' function to handle multiple connections concurrently.

For processing requests, The recv function is used to receive data over stream sockets or CONNECTED datagram sockets. Then, we read data using sockets and stored information in buffer. The, we check the HTTP version and HTTP method, and handle some basic errors. Then we parse the URI and requested headers. After that, we handle different requests by distinguish whether it is GET, POST, HEAD, and then back to client everything when finishing a request. 






