# IC-Web-Server
## Milestone 1: Sequential GET/HEAD Requests for Static Content [30 points]
Due: Wed June 9

You will extend the parser in the starter code to parse HTTP requests. Your code can robustly distinguish between proper requests and malformed requests. Your server can handle sequential requests from a user-specified port and respond suitably. If the request is GET or HEAD and the requested object is static, the server will return the appropriate data. In short, you will have a fully functioning Web server for static contents, except it cannot yet handle concurrent requests.


## Milestone 2: High-Throughput Concurrent Requests Using a Thread Pool [45 points]
Due: Mon June 21

You will extend your Web server to serve multiple requests at once. More specifically, whereas the server at the end of Milestone 1 is only capable of handling one request at a time, your server for Milestone 2 will be able to handle tens of hundreds of requests per second. You will use a thread-based design (you learned about threads earlier and this is your chance to deeply understand it.) For performance, you are expected to design and implement a thread pool, so each request will be handled by a thread worker in the pool.


## Updated: Milestone 3: POST Request and Dynamic Content via CGI [25 points]
Due: Wed June 30

You will your server to handle POST requests, as well as dynamic content via CGI. This means when a request of this kind comes in, the server will run the corresponding Linux program following the common gateway interface (CGI) protocol, as specified by RFC3875. Your server is expected to continue to be concurrent—for both static and dynamic requests.
