// HTTPS MITM Proxy with LLM Enhancement Settings

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <sys/select.h>
#include <fcntl.h>
#include <time.h>
// openssl libraries, necessary for SSL/TLS and certificate operations
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#define BUFFER_SIZE 65536
#define MAX_HEADER_SIZE 8192
#define BACKLOG 128
#define FLASK_HOST "127.0.0.1"
#define FLASK_PORT 5000

// global CA certificate and key for signing fake certificates
static X509 *ca_cert = NULL;
static EVP_PKEY *ca_key = NULL;

// LLM functionality enable/disable flag
static int llm_enabled = 0;

// Flask availability flag (avoid repeated connection attempts if Flask is down)
static int flask_available = 1;
static time_t last_flask_check = 0;

// client connection context
typedef struct {
    int client_fd;
    struct sockaddr_in client_addr;
} client_context_t;

// function prototypes
void *handle_client(void *arg);
void handle_http_request(int client_fd, char *request, size_t req_len);
void handle_https_connect(int client_fd, char *request, size_t req_len);
int connect_to_server(const char *hostname, int port);
X509 *generate_cert(const char *hostname);
SSL_CTX *create_ssl_context_server(void);
SSL_CTX *create_ssl_context_client(void);
void inject_header(char *response, size_t *response_len, size_t buffer_size);
int read_line(int fd, char *buf, size_t max_len);
void parse_host_port(char *host_header, char **hostname, int *port);
int load_ca_cert_and_key(const char *cert_path, const char *key_path);
char *send_to_flask_enhance(const char *html, const char *url, size_t *out_len);
int is_html_content_type(const char *content_type);
char *extract_content_type(const char *headers);
char *base64_encode(const unsigned char *input, size_t length);
unsigned char *base64_decode(const char *input, size_t *out_len);

// signal handler to prevent crashes when connection breaks
void signal_handler(int sig) {
    if (sig == SIGPIPE) {
        return;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4 || argc > 5) {
        fprintf(stderr, "Usage: %s <port> <ca_cert_path> <ca_key_path> [llm=true]\n", argv[0]);
        fprintf(stderr, "  Optional: llm=true to enable LLM functionality\n");
        exit(EXIT_FAILURE);
    }

    int port = atoi(argv[1]);
    const char *ca_cert_path = argv[2];
    const char *ca_key_path = argv[3];
    
    // Parse optional llm parameter
    if (argc == 5) {
        if (strcmp(argv[4], "llm=true") == 0) {
            llm_enabled = 1;
            printf("✅ LLM functionality ENABLED\n");
        } else if (strcmp(argv[4], "llm=false") == 0) {
            llm_enabled = 0;
            printf("⏭️  LLM functionality DISABLED\n");
        } else {
            fprintf(stderr, "Warning: Unknown parameter '%s', LLM disabled\n", argv[4]);
            llm_enabled = 0;
        }
    } else {
        llm_enabled = 0;
        printf("⏭️  LLM functionality DISABLED (default)\n");
    }

    // load CA certificate and key
    if (load_ca_cert_and_key(ca_cert_path, ca_key_path) != 0) {
        fprintf(stderr, "Failed to load CA certificate and key\n");
        exit(EXIT_FAILURE);
    }

    // initialize OpenSSL
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    signal(SIGPIPE, signal_handler);

    // create listening socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // bind socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(listen_fd, BACKLOG) < 0) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    printf("Proxy server listening on port %d\n", port);

    // main loop: accept and handle client connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        client_context_t *ctx = malloc(sizeof(client_context_t));
        if (!ctx) {
            close(client_fd);
            continue;
        }
        ctx->client_fd = client_fd;
        ctx->client_addr = client_addr;

        // handle each client in a separate thread
        pthread_t thread;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        
        if (pthread_create(&thread, &attr, handle_client, ctx) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(ctx);
        }
        pthread_attr_destroy(&attr);
    }

    close(listen_fd);
    X509_free(ca_cert);
    EVP_PKEY_free(ca_key);
    return 0;
}

// load CA certificate and key from files
int load_ca_cert_and_key(const char *cert_path, const char *key_path) {
    FILE *fp = fopen(cert_path, "r");
    if (!fp) {
        perror("fopen ca_cert");
        return -1;
    }
    ca_cert = PEM_read_X509(fp, NULL, NULL, NULL);
    fclose(fp);
    if (!ca_cert) {
        fprintf(stderr, "Failed to read CA certificate\n");
        return -1;
    }

    fp = fopen(key_path, "r");
    if (!fp) {
        perror("fopen ca_key");
        X509_free(ca_cert);
        ca_cert = NULL;
        return -1;
    }
    ca_key = PEM_read_PrivateKey(fp, NULL, NULL, NULL);
    fclose(fp);
    if (!ca_key) {
        fprintf(stderr, "Failed to read CA key\n");
        X509_free(ca_cert);
        ca_cert = NULL;
        return -1;
    }

    return 0;
}

// handle client connection in separate thread
void *handle_client(void *arg) {
    client_context_t *ctx = (client_context_t *)arg;
    int client_fd = ctx->client_fd;
    free(ctx);

    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    char request[MAX_HEADER_SIZE];
    ssize_t bytes_read = recv(client_fd, request, sizeof(request) - 1, 0);

    if (bytes_read <= 0) {
        close(client_fd);
        return NULL;
    }

    request[bytes_read] = '\0';

    // check if CONNECT request for HTTPS, otherwise handle as HTTP
    if (strncmp(request, "CONNECT ", 8) == 0) {
        handle_https_connect(client_fd, request, bytes_read);
    } else if (strncmp(request, "GET ", 4) == 0 || 
               strncmp(request, "POST ", 5) == 0 ||
               strncmp(request, "HEAD ", 5) == 0) {
        handle_http_request(client_fd, request, bytes_read);
    } else {
        const char *err = "HTTP/1.1 501 Not Implemented\r\n\r\n";
        send(client_fd, err, strlen(err), 0);
    }

    close(client_fd);
    return NULL;
}

// handle plain HTTP requests
void handle_http_request(int client_fd, char *request, size_t req_len) {
    char method[16], url[2048], version[16];
    char hostname[256] = {0};
    int port = 80;
    char *host_line = NULL;

    if (sscanf(request, "%15s %2047s %15s", method, url, version) != 3) {
        const char *err = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(client_fd, err, strlen(err), 0);
        return;
    }

    // extract hostname from Host header
    host_line = strstr(request, "Host: ");
    if (host_line) {
        host_line += 6;
        char *end = strstr(host_line, "\r\n");
        if (end) {
            size_t len = end - host_line;
            if (len < sizeof(hostname)) {
                strncpy(hostname, host_line, len);
                hostname[len] = '\0';
                parse_host_port(hostname, NULL, &port);
            }
        }
    }

    if (hostname[0] == '\0') {
        const char *err = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(client_fd, err, strlen(err), 0);
        return;
    }

    // Remove Accept-Encoding header if LLM is enabled (to prevent gzip)
    if (llm_enabled) {
        char *accept_encoding = strcasestr(request, "\r\nAccept-Encoding:");
        if (accept_encoding) {
            char *line_end = strstr(accept_encoding + 2, "\r\n");
            if (line_end) {
                memmove(accept_encoding, line_end, strlen(line_end) + 1);
                req_len = strlen(request);
            }
        }
    }

    // connect to the target server
    int server_fd = connect_to_server(hostname, port);
    if (server_fd < 0) {
        const char *err = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
        send(client_fd, err, strlen(err), 0);
        return;
    }

    // forward request to server
    ssize_t sent = 0;
    while (sent < (ssize_t)req_len) {
        ssize_t n = send(server_fd, request + sent, req_len - sent, 0);
        if (n < 0) {
            close(server_fd);
            return;
        }
        sent += n;
    }

    if (!llm_enabled) {
        // Simple mode: stream response with header injection
        char response[BUFFER_SIZE];
        ssize_t total_bytes = 0;
        int first_chunk = 1;
        
        while (1) {
            ssize_t bytes = recv(server_fd, response, sizeof(response), 0);
            if (bytes <= 0) {
                break;
            }
            
            size_t response_len = bytes;
            
            // inject X-Proxy header in first chunk
            if (first_chunk && bytes >= 5 && strncmp(response, "HTTP/", 5) == 0) {
                inject_header(response, &response_len, sizeof(response));
                first_chunk = 0;
            }
            
            ssize_t sent_to_client = 0;
            while (sent_to_client < (ssize_t)response_len) {
                ssize_t n = send(client_fd, response + sent_to_client, 
                               response_len - sent_to_client, 0);
                if (n < 0) {
                    close(server_fd);
                    return;
                }
                sent_to_client += n;
            }
            
            total_bytes += bytes;
        }
    } else {
        // LLM mode: collect full response, enhance if HTML
        size_t max_response = 2 * 1024 * 1024;
        char *full_response = malloc(max_response);
        if (!full_response) {
            close(server_fd);
            return;
        }
        
        ssize_t total_received = 0;
        ssize_t bytes;
        
        while ((bytes = recv(server_fd, full_response + total_received, 
                            max_response - total_received - 1, 0)) > 0) {
            total_received += bytes;
            if (total_received >= max_response - 1) {
                break;
            }
        }
        full_response[total_received] = '\0';
        
        // Find headers end
        char *headers_end = strstr(full_response, "\r\n\r\n");
        int should_enhance = 0;
        
        if (headers_end && total_received > 0) {
            char *body = headers_end + 4;
            size_t body_len = total_received - (body - full_response);
            
            // Check if this is HTML and not compressed
            char *content_type = extract_content_type(full_response);
            int is_html = is_html_content_type(content_type);
            int is_compressed = (strcasestr(full_response, "Content-Encoding:") != NULL);
            
            if (content_type) free(content_type);
            
            should_enhance = (is_html && !is_compressed && body_len > 0 && body_len < max_response);
        }
        
        if (should_enhance) {
            // Build full URL
            char full_url[4096];
            snprintf(full_url, sizeof(full_url), "http://%s%s", hostname, url);
            
            // Send to Flask for enhancement
            char *body = headers_end + 4;
            size_t enhanced_len = 0;
            char *enhanced_html = send_to_flask_enhance(body, full_url, &enhanced_len);
            
            if (enhanced_html && enhanced_len > 0 && enhanced_len < max_response) {
                // Build new response with enhanced HTML
                char *new_response = malloc(max_response);
                if (new_response) {
                    // Extract status line
                    char *status_end = strstr(full_response, "\r\n");
                    if (status_end) {
                        size_t status_len = status_end - full_response;
                        memcpy(new_response, full_response, status_len);
                        
                        // Add headers with enhanced content
                        int resp_len = status_len;
                        resp_len += snprintf(new_response + resp_len, max_response - resp_len,
                            "\r\nX-Proxy:CS112\r\n"
                            "Content-Length: %zu\r\n"
                            "Content-Type: text/html; charset=utf-8\r\n"
                            "Connection: close\r\n"
                            "\r\n", enhanced_len);
                        
                        memcpy(new_response + resp_len, enhanced_html, enhanced_len);
                        resp_len += enhanced_len;
                        
                        // Send enhanced response to client
                        send(client_fd, new_response, resp_len, 0);
                        
                        free(new_response);
                        free(enhanced_html);
                        free(full_response);
                        close(server_fd);
                        return;
                    }
                    free(new_response);
                }
                free(enhanced_html);
            }
        }
        
        // Fallback: inject header and send original response
        size_t resp_len = total_received;
        if (total_received >= 5 && strncmp(full_response, "HTTP/", 5) == 0) {
            inject_header(full_response, &resp_len, max_response);
        }
        
        send(client_fd, full_response, resp_len, 0);
        free(full_response);
    }

    close(server_fd);
}

// handle HTTPS CONNECT requests with TLS interception
void handle_https_connect(int client_fd, char *request, size_t req_len) {
    char hostname[256], port_str[16];
    int port = 443;

    // parse CONNECT request: CONNECT hostname:port HTTP/1.1
    char *space = strchr(request + 8, ' ');
    if (!space) {
        const char *err = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(client_fd, err, strlen(err), 0);
        return;
    }

    size_t host_len = space - (request + 8);
    if (host_len >= sizeof(hostname)) {
        const char *err = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(client_fd, err, strlen(err), 0);
        return;
    }

    strncpy(hostname, request + 8, host_len);
    hostname[host_len] = '\0';

    char *colon = strchr(hostname, ':');
    if (colon) {
        *colon = '\0';
        port = atoi(colon + 1);
    }

    // connect to the upstream HTTPS server
    int server_fd = connect_to_server(hostname, port);
    if (server_fd < 0) {
        const char *err = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
        send(client_fd, err, strlen(err), 0);
        return;
    }

    // create SSL connection to upstream server (proxy acts as client)
    SSL_CTX *server_ctx = create_ssl_context_client();
    if (!server_ctx) {
        fprintf(stderr, "Failed to create server SSL context\n");
        close(server_fd);
        const char *err = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
        send(client_fd, err, strlen(err), 0);
        return;
    }

    SSL *server_ssl = SSL_new(server_ctx);
    if (!server_ssl) {
        fprintf(stderr, "Failed to create server SSL object\n");
        SSL_CTX_free(server_ctx);
        close(server_fd);
        const char *err = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
        send(client_fd, err, strlen(err), 0);
        return;
    }

    SSL_set_fd(server_ssl, server_fd);
    SSL_set_tlsext_host_name(server_ssl, hostname);

    int ssl_ret = SSL_connect(server_ssl);
    if (ssl_ret <= 0) {
        SSL_free(server_ssl);
        SSL_CTX_free(server_ctx);
        close(server_fd);
        const char *err = "HTTP/1.1 502 Bad Gateway\r\n\r\n";
        send(client_fd, err, strlen(err), 0);
        return;
    }

    // send 200 Connection Established to client
    const char *response = "HTTP/1.1 200 Connection Established\r\n\r\n";
    send(client_fd, response, strlen(response), 0);

    // create SSL context for client connection (proxy acts as server)
    SSL_CTX *client_ctx = create_ssl_context_server();
    if (!client_ctx) {
        SSL_shutdown(server_ssl);
        SSL_free(server_ssl);
        SSL_CTX_free(server_ctx);
        close(server_fd);
        return;
    }

    // generate fake certificate for this hostname
    X509 *cert = generate_cert(hostname);
    if (!cert) {
        SSL_CTX_free(client_ctx);
        SSL_shutdown(server_ssl);
        SSL_free(server_ssl);
        SSL_CTX_free(server_ctx);
        close(server_fd);
        return;
    }

    SSL_CTX_use_certificate(client_ctx, cert);
    SSL_CTX_use_PrivateKey(client_ctx, ca_key);

    SSL *client_ssl = SSL_new(client_ctx);
    if (!client_ssl) {
        fprintf(stderr, "Failed to create client SSL object\n");
        SSL_CTX_free(client_ctx);
        X509_free(cert);
        SSL_shutdown(server_ssl);
        SSL_free(server_ssl);
        SSL_CTX_free(server_ctx);
        close(server_fd);
        return;
    }

    SSL_set_fd(client_ssl, client_fd);

    int accept_ret = SSL_accept(client_ssl);
    if (accept_ret <= 0) {
        SSL_free(client_ssl);
        SSL_CTX_free(client_ctx);
        X509_free(cert);
        SSL_shutdown(server_ssl);
        SSL_free(server_ssl);
        SSL_CTX_free(server_ctx);
        close(server_fd);
        return;
    }

    // Choose processing mode based on llm_enabled flag
    if (!llm_enabled) {
        // Simple relay mode: fast pass-through with header injection only
        fd_set readfds;
        int max_fd = (client_fd > server_fd) ? client_fd : server_fd;
        char client_buffer[BUFFER_SIZE];
        char server_buffer[BUFFER_SIZE];
        int active = 1;
        int first_response = 1;

        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
        flags = fcntl(server_fd, F_GETFL, 0);
        fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);

        while (active) {
            FD_ZERO(&readfds);
            FD_SET(client_fd, &readfds);
            FD_SET(server_fd, &readfds);

            struct timeval timeout;
            timeout.tv_sec = 60;
            timeout.tv_usec = 0;

            int ret = select(max_fd + 1, &readfds, NULL, NULL, &timeout);
            if (ret < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (ret == 0) {
                break;
            }

            // relay data from client to server
            if (FD_ISSET(client_fd, &readfds)) {
                int bytes = SSL_read(client_ssl, client_buffer, sizeof(client_buffer));
                if (bytes <= 0) {
                    int err = SSL_get_error(client_ssl, bytes);
                    if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                        break;
                    }
                } else {
                    int sent = 0;
                    while (sent < bytes) {
                        int n = SSL_write(server_ssl, client_buffer + sent, bytes - sent);
                        if (n <= 0) {
                            int err = SSL_get_error(server_ssl, n);
                            if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                                active = 0;
                                break;
                            }
                        } else {
                            sent += n;
                        }
                    }
                }
            }

            // relay data from server to client and inject header
            if (FD_ISSET(server_fd, &readfds) && active) {
                int bytes = SSL_read(server_ssl, server_buffer, sizeof(server_buffer));
                if (bytes <= 0) {
                    int err = SSL_get_error(server_ssl, bytes);
                    if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                        break;
                    }
                } else {
                    size_t len = bytes;
                    if (first_response && bytes >= 5 && strncmp(server_buffer, "HTTP/", 5) == 0) {
                        inject_header(server_buffer, &len, sizeof(server_buffer));
                        first_response = 0;
                    }
                    
                    int sent = 0;
                    while (sent < (int)len) {
                        int n = SSL_write(client_ssl, server_buffer + sent, len - sent);
                        if (n <= 0) {
                            int err = SSL_get_error(client_ssl, n);
                            if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
                                active = 0;
                                break;
                            }
                        } else {
                            sent += n;
                        }
                    }
                }
            }
        }
    } else {
        // LLM mode: collect full response, process with Flask, and send enhanced version
        // Read client request first
        char *client_request = malloc(BUFFER_SIZE);
        if (!client_request) {
            SSL_shutdown(client_ssl);
            SSL_free(client_ssl);
            SSL_CTX_free(client_ctx);
            X509_free(cert);
            SSL_shutdown(server_ssl);
            SSL_free(server_ssl);
            SSL_CTX_free(server_ctx);
            close(server_fd);
            return;
        }
        
        int request_bytes = SSL_read(client_ssl, client_request, BUFFER_SIZE - 1);
        if (request_bytes <= 0) {
            free(client_request);
            SSL_shutdown(client_ssl);
            SSL_free(client_ssl);
            SSL_CTX_free(client_ctx);
            X509_free(cert);
            SSL_shutdown(server_ssl);
            SSL_free(server_ssl);
            SSL_CTX_free(server_ctx);
            close(server_fd);
            return;
        }
        client_request[request_bytes] = '\0';
        
        // Remove Accept-Encoding header to prevent gzip compression
        char *accept_encoding = strcasestr(client_request, "\r\nAccept-Encoding:");
        if (accept_encoding) {
            char *line_end = strstr(accept_encoding + 2, "\r\n");
            if (line_end) {
                memmove(accept_encoding, line_end, strlen(line_end) + 1);
                request_bytes = strlen(client_request);
            }
        }
        
        // Forward modified request to server
        int sent = 0;
        while (sent < request_bytes) {
            int n = SSL_write(server_ssl, client_request + sent, request_bytes - sent);
            if (n <= 0) {
                free(client_request);
                SSL_shutdown(client_ssl);
                SSL_free(client_ssl);
                SSL_CTX_free(client_ctx);
                X509_free(cert);
                SSL_shutdown(server_ssl);
                SSL_free(server_ssl);
                SSL_CTX_free(server_ctx);
                close(server_fd);
                return;
            }
            sent += n;
        }
        
        // Collect full response from server (up to 2MB)
        size_t max_response = 2 * 1024 * 1024;
        char *full_response = malloc(max_response);
        if (!full_response) {
            free(client_request);
            SSL_shutdown(client_ssl);
            SSL_free(client_ssl);
            SSL_CTX_free(client_ctx);
            X509_free(cert);
            SSL_shutdown(server_ssl);
            SSL_free(server_ssl);
            SSL_CTX_free(server_ctx);
            close(server_fd);
            return;
        }
        
        size_t total_received = 0;
        int bytes;
        while ((bytes = SSL_read(server_ssl, full_response + total_received,
                                max_response - total_received - 1)) > 0) {
            total_received += bytes;
            if (total_received >= max_response - 1) {
                break;
            }
            
            // Simple check if we have complete response
            if (total_received > 4 && strstr(full_response, "\r\n\r\n")) {
                // Check if we have Content-Length header
                char *cl_header = strcasestr(full_response, "Content-Length:");
                if (cl_header) {
                    cl_header += 15;
                    while (*cl_header == ' ') cl_header++;
                    size_t content_length = atoll(cl_header);
                    
                    char *body_start = strstr(full_response, "\r\n\r\n");
                    if (body_start) {
                        body_start += 4;
                        size_t body_received = total_received - (body_start - full_response);
                        if (body_received >= content_length) {
                            break; // Complete response received
                        }
                    }
                }
            }
        }
        full_response[total_received] = '\0';
        
        // Parse headers and body
        char *headers_end = strstr(full_response, "\r\n\r\n");
        int should_enhance = 0;
        
        if (headers_end && total_received > 0) {
            char *body = headers_end + 4;
            size_t body_len = total_received - (body - full_response);
            
            // Check if this is HTML and not compressed
            char *content_type = extract_content_type(full_response);
            int is_html = is_html_content_type(content_type);
            int is_compressed = (strcasestr(full_response, "Content-Encoding:") != NULL);
            
            if (content_type) free(content_type);
            
            should_enhance = (is_html && !is_compressed && body_len > 0 && body_len < max_response);
        }
        
        if (should_enhance) {
            // Extract URL from client request
            char url[2048] = "";
            char *url_start = strchr(client_request, ' ');
            if (url_start) {
                url_start++;
                char *url_end = strchr(url_start, ' ');
                if (url_end) {
                    size_t url_len = url_end - url_start;
                    if (url_len < sizeof(url)) {
                        strncpy(url, url_start, url_len);
                        url[url_len] = '\0';
                    }
                }
            }
            
            // Build full URL
            char full_url[4096];
            snprintf(full_url, sizeof(full_url), "https://%s%s", hostname, url);
            
            // Send to Flask for enhancement
            char *body = strstr(full_response, "\r\n\r\n") + 4;
            size_t enhanced_len = 0;
            char *enhanced_html = send_to_flask_enhance(body, full_url, &enhanced_len);
            
            if (enhanced_html && enhanced_len > 0 && enhanced_len < max_response) {
                // Build new response with enhanced HTML
                char *new_response = malloc(max_response);
                if (new_response) {
                    // Extract status line
                    char *status_end = strstr(full_response, "\r\n");
                    if (status_end) {
                        size_t status_len = status_end - full_response;
                        memcpy(new_response, full_response, status_len);
                        
                        // Add headers with enhanced content
                        int resp_len = status_len;
                        resp_len += snprintf(new_response + resp_len, max_response - resp_len,
                            "\r\nX-Proxy:CS112\r\n"
                            "Content-Length: %zu\r\n"
                            "Content-Type: text/html; charset=utf-8\r\n"
                            "Connection: close\r\n"
                            "\r\n", enhanced_len);
                        
                        memcpy(new_response + resp_len, enhanced_html, enhanced_len);
                        resp_len += enhanced_len;
                        
                        // Send enhanced response to client
                        sent = 0;
                        while (sent < resp_len) {
                            int n = SSL_write(client_ssl, new_response + sent, resp_len - sent);
                            if (n <= 0) break;
                            sent += n;
                        }
                        
                        free(new_response);
                        free(enhanced_html);
                        free(full_response);
                        free(client_request);
                        SSL_shutdown(client_ssl);
                        SSL_free(client_ssl);
                        SSL_CTX_free(client_ctx);
                        X509_free(cert);
                        SSL_shutdown(server_ssl);
                        SSL_free(server_ssl);
                        SSL_CTX_free(server_ctx);
                        close(server_fd);
                        return;
                    }
                    free(new_response);
                }
                free(enhanced_html);
            }
        }
        
        // Fallback: inject header and send original response
        size_t resp_len = total_received;
        if (total_received >= 5 && strncmp(full_response, "HTTP/", 5) == 0) {
            inject_header(full_response, &resp_len, max_response);
        }
        
        sent = 0;
        while (sent < (int)resp_len) {
            int n = SSL_write(client_ssl, full_response + sent, resp_len - sent);
            if (n <= 0) break;
            sent += n;
        }
        
        free(full_response);
        free(client_request);
    }

    // cleanup SSL connections
    SSL_shutdown(client_ssl);
    SSL_free(client_ssl);
    SSL_CTX_free(client_ctx);
    X509_free(cert);
    SSL_shutdown(server_ssl);
    SSL_free(server_ssl);
    SSL_CTX_free(server_ctx);
    close(server_fd);
}

// connect to upstream server
int connect_to_server(const char *hostname, int port) {
    struct hostent *host = gethostbyname(hostname);
    if (!host) {
        return -1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return -1;
    }

    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr, host->h_addr_list[0], host->h_length);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sockfd);
        return -1;
    }

    return sockfd;
}

// create SSL context for server mode (proxy acts as server to client)
SSL_CTX *create_ssl_context_server(void) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        return NULL;
    }
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    return ctx;
}

// create SSL context for client mode (proxy acts as client to server)
SSL_CTX *create_ssl_context_client(void) {
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        return NULL;
    }
    SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    return ctx;
}

// generate fake certificate for hostname, signed by CA
X509 *generate_cert(const char *hostname) {
    X509 *cert = X509_new();
    if (!cert) {
        fprintf(stderr, "Failed to create X509 certificate\n");
        return NULL;
    }

    if (!X509_set_version(cert, 2)) {
        fprintf(stderr, "Failed to set certificate version\n");
        X509_free(cert);
        return NULL;
    }

    // set serial number based on time and hostname
    unsigned long serial = (unsigned long)time(NULL);
    for (size_t i = 0; hostname[i] != '\0'; i++) {
        serial = serial * 31 + hostname[i];
    }
    ASN1_INTEGER_set(X509_get_serialNumber(cert), serial);

    // set validity: start 1 day in past to handle clock skew, valid for 1 year
    X509_gmtime_adj(X509_get_notBefore(cert), -86400L);
    X509_gmtime_adj(X509_get_notAfter(cert), 31536000L);

    // set certificate subject
    X509_NAME *name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, 
                               (unsigned char *)"US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, 
                               (unsigned char *)"CS112 Proxy", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, 
                               (unsigned char *)hostname, -1, -1, 0);

    X509_set_issuer_name(cert, X509_get_subject_name(ca_cert));

    // set public key
    if (!X509_set_pubkey(cert, ca_key)) {
        fprintf(stderr, "Failed to set public key\n");
        X509_free(cert);
        return NULL;
    }

    // add X.509 v3 extensions
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, ca_cert, cert, NULL, NULL, 0);
    
    X509_EXTENSION *ext = X509V3_EXT_conf_nid(NULL, &ctx, 
                                              NID_basic_constraints, 
                                              "CA:FALSE");
    if (ext) {
        X509_add_ext(cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    ext = X509V3_EXT_conf_nid(NULL, &ctx, NID_key_usage, 
                              "digitalSignature,keyEncipherment");
    if (ext) {
        X509_add_ext(cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    ext = X509V3_EXT_conf_nid(NULL, &ctx, NID_ext_key_usage, 
                              "serverAuth");
    if (ext) {
        X509_add_ext(cert, ext, -1);
        X509_EXTENSION_free(ext);
    }

    // add SAN extension, critical for modern browsers
    char san[512];
    snprintf(san, sizeof(san), "DNS:%s", hostname);
    ext = X509V3_EXT_conf_nid(NULL, &ctx, NID_subject_alt_name, san);
    if (ext) {
        X509_add_ext(cert, ext, -1);
        X509_EXTENSION_free(ext);
    } else {
        fprintf(stderr, "Warning: Failed to add SAN extension for %s\n", hostname);
    }

    // sign the certificate with CA key
    if (!X509_sign(cert, ca_key, EVP_sha256())) {
        fprintf(stderr, "Failed to sign certificate\n");
        ERR_print_errors_fp(stderr);
        X509_free(cert);
        return NULL;
    }

    return cert;
}

// inject X-Proxy:CS112 header into HTTP response
void inject_header(char *response, size_t *response_len, size_t buffer_size) {
    char *header_end = strstr(response, "\r\n");
    if (!header_end) {
        return;
    }

    if (strstr(response, "X-Proxy:")) {
        return;
    }

    const char *new_header = "X-Proxy:CS112\r\n";
    size_t new_header_len = strlen(new_header);
    size_t insert_pos = header_end + 2 - response;

    if (*response_len + new_header_len >= buffer_size) {
        return;
    }

    memmove(response + insert_pos + new_header_len,
            response + insert_pos,
            *response_len - insert_pos);

    memcpy(response + insert_pos, new_header, new_header_len);
    *response_len += new_header_len;
}

// parse hostname and port from Host header
void parse_host_port(char *host_header, char **hostname, int *port) {
    char *colon = strchr(host_header, ':');
    if (colon) {
        *colon = '\0';
        *port = atoi(colon + 1);
    }
}

// Extract Content-Type header from HTTP headers
char *extract_content_type(const char *headers) {
    if (!headers) {
        return NULL;
    }
    
    const char *ct_start = strcasestr(headers, "Content-Type:");
    if (!ct_start) {
        ct_start = strcasestr(headers, "content-type:");
    }
    if (!ct_start) {
        return NULL;
    }
    
    ct_start += 13;  // Skip "Content-Type:"
    while (*ct_start == ' ') ct_start++;
    
    const char *ct_end = strstr(ct_start, "\r\n");
    if (!ct_end) {
        ct_end = strstr(ct_start, "\n");
    }
    if (!ct_end) {
        return NULL;
    }
    
    size_t len = ct_end - ct_start;
    char *content_type = malloc(len + 1);
    if (!content_type) {
        return NULL;
    }
    
    strncpy(content_type, ct_start, len);
    content_type[len] = '\0';
    
    return content_type;
}

// Check if Content-Type is HTML
int is_html_content_type(const char *content_type) {
    if (!content_type) {
        return 0;
    }
    return (strcasestr(content_type, "text/html") != NULL);
}

// Send HTML to Flask /enhance endpoint for LLM processing
char *send_to_flask_enhance(const char *html, const char *url, size_t *out_len) {
    // Skip if Flask is known to be unavailable (check every 60 seconds)
    time_t now = time(NULL);
    if (!flask_available && (now - last_flask_check) < 60) {
        return NULL;
    }
    
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return NULL;
    }
    
    struct sockaddr_in flask_addr;
    memset(&flask_addr, 0, sizeof(flask_addr));
    flask_addr.sin_family = AF_INET;
    flask_addr.sin_port = htons(FLASK_PORT);
    inet_pton(AF_INET, FLASK_HOST, &flask_addr.sin_addr);
    
    struct timeval timeout;
    timeout.tv_sec = 0;  // Very short timeout - Flask just injects JS
    timeout.tv_usec = 500000;  // 500ms should be enough for JS injection
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    if (connect(sockfd, (struct sockaddr *)&flask_addr, sizeof(flask_addr)) < 0) {
        close(sockfd);
        flask_available = 0;
        last_flask_check = now;
        return NULL;
    }
    
    // Flask is available
    flask_available = 1;
    
    // Encode HTML as base64 to avoid Unicode/encoding issues
    size_t html_len = strlen(html);
    size_t url_len = url ? strlen(url) : 0;
    
    // Calculate base64 size
    size_t b64_buf_len = ((html_len + 2) / 3) * 4 + 1;
    char *html_b64 = malloc(b64_buf_len);
    if (!html_b64) {
        close(sockfd);
        return NULL;
    }
    
    // Encode HTML to base64 using OpenSSL
    int encoded_len = EVP_EncodeBlock((unsigned char*)html_b64,
                                      (unsigned char*)html,
                                      html_len);
    html_b64[encoded_len] = '\0';
    
    // Build simple JSON with base64
    size_t json_size = encoded_len + url_len + 1024;
    char *json_body = malloc(json_size);
    if (!json_body) {
        free(html_b64);
        close(sockfd);
        return NULL;
    }
    
    strcpy(json_body, "{\"html_base64\":\"");
    strcat(json_body, html_b64);
    strcat(json_body, "\",\"url\":\"");
    if (url) {
        strcat(json_body, url);
    }
    strcat(json_body, "\"}");
    
    free(html_b64);
    size_t json_len = strlen(json_body);
    
    // Build HTTP request
    char http_request[2048];
    snprintf(http_request, sizeof(http_request),
             "POST /enhance HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n",
             FLASK_HOST, FLASK_PORT, json_len);
    
    // Send request
    send(sockfd, http_request, strlen(http_request), 0);
    send(sockfd, json_body, json_len, 0);
    free(json_body);
    
    // Receive response (limit to 2MB)
    size_t max_response = 2 * 1024 * 1024;
    char *response = malloc(max_response);
    if (!response) {
        close(sockfd);
        return NULL;
    }
    
    size_t total_received = 0;
    ssize_t bytes;
    while ((bytes = recv(sockfd, response + total_received, 
                        max_response - total_received - 1, 0)) > 0) {
        total_received += bytes;
        if (total_received >= max_response - 1) {
            break;
        }
    }
    response[total_received] = '\0';
    close(sockfd);
    
    // Flask returns base64-encoded HTML to avoid Unicode issues
    // Parse JSON response
    char *json_start = strstr(response, "\r\n\r\n");
    if (!json_start) {
        free(response);
        return NULL;
    }
    json_start += 4;
    
    // Find "html_base64" field
    char *b64_field = strstr(json_start, "\"html_base64\":");
    if (!b64_field) {
        free(response);
        return NULL;
    }
    b64_field += 14;
    while (*b64_field == ' ') b64_field++;
    
    if (*b64_field != '"') {
        free(response);
        return NULL;
    }
    b64_field++;
    
    // Find end of base64 string
    char *b64_end = b64_field;
    while (*b64_end != '\0' && *b64_end != '"') {
        b64_end++;
    }
    
    size_t b64_len = b64_end - b64_field;
    
    // Decode base64
    char *decoded = malloc(b64_len);  // decoded will be smaller than encoded
    if (!decoded) {
        free(response);
        return NULL;
    }
    
    // Use OpenSSL's base64 decode
    int decoded_len = EVP_DecodeBlock((unsigned char*)decoded, 
                                      (unsigned char*)b64_field, 
                                      b64_len);
    
    if (decoded_len < 0) {
        free(decoded);
        free(response);
        return NULL;
    }
    
    // EVP_DecodeBlock includes padding, calculate actual length
    // Count '=' padding chars in base64 string
    int padding = 0;
    if (b64_len > 0 && b64_field[b64_len - 1] == '=') padding++;
    if (b64_len > 1 && b64_field[b64_len - 2] == '=') padding++;
    
    decoded_len -= padding;
    
    // Ensure null termination
    if (decoded_len >= 0) {
        decoded[decoded_len] = '\0';
    } else {
        decoded_len = 0;
        decoded[0] = '\0';
    }
    
    *out_len = decoded_len;
    free(response);
    
    return decoded;
}

