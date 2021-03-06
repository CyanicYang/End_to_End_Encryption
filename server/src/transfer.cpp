#include "../include/transfer.hpp"

using namespace std;
using namespace fly;

extern PresentationLayer PreLayerInstance;

StatusCode TransferLayer::try_recv(Client &client) {
    uint8_t tmp_buffer[kMaxPacketLength] {};

    // at least receive more than header so we can determine the data length
    if (client.recv_buffer.size() < kHeaderSize) { // doesn't have header currently
        int received_bytes = 0;
        while (client.recv_buffer.size() < kHeaderSize) {
            LOG(Level::Debug) << "try_recv socket_fd: " << client.socket_fd << endl;
            int num_bytes = recv(client.socket_fd, tmp_buffer, client.recv_buffer.get_num_free_bytes(), 0);
            // error handling
            if (num_bytes <= 0) {
                LERR << "?հ?????\n";
                return StatusCode::RecvError;
            } else {
                // recv correct
                received_bytes += num_bytes;
                client.recv_buffer.enqueue(tmp_buffer, received_bytes); // TODO
            }
        }
        return StatusCode::OK;
    } 

    if(client.is_scr == false) {
    if (!client.recv_buffer.is_full() && client.recv_buffer.current_packet_size()) {
        int num_bytes = recv(client.socket_fd, tmp_buffer, client.recv_buffer.get_num_free_bytes(), 0);
        // error handling
        if (num_bytes < 0) {
            LERR << client.socket_fd << endl;
            perror("RecvError 2\n");
            return StatusCode::RecvError;
        } else {
            client.recv_buffer.enqueue(tmp_buffer, num_bytes); 
        }
    }
    }
    else {
        if (!client.recv_buffer.is_full() && client.scr_packet_size){
            int num_bytes = recv(client.socket_fd, tmp_buffer, client.recv_buffer.get_num_free_bytes(), 0);
        // error handling
        if (num_bytes < 0) {
            LERR << client.socket_fd << endl;
            perror("RecvError 2\n");
            return StatusCode::RecvError;
        } else {
            client.recv_buffer.enqueue(tmp_buffer, num_bytes); 
        }
        }
    }

    return StatusCode::OK;
}

StatusCode TransferLayer::try_send(Client &client) {
    if (!client.send_buffer.size()) {
        return StatusCode::OK;
    }
    vector<uint8_t> &v = client.send_buffer.front();
    size_t size_before = v.size();
    int num_bytes = send(client.socket_fd, v.data(), size_before, MSG_NOSIGNAL);
    if (num_bytes <= 0) {
        LERR << "???????󣬷???ֵ=" << num_bytes << endl;
        return StatusCode::SendError;
    }

    if (num_bytes < size_before) {
        // partial send
        v.erase(v.begin(), v.begin() + num_bytes);
    } else { // complete send
        client.send_buffer.pop();
    }

    return StatusCode::OK;
}

void TransferLayer::select_loop(int listener) {
    fd_set read_fds, write_fds;

    for (int i = 0; ; i++) {
        int fdmax = reset_rw_fd_sets(read_fds, write_fds);
        FD_SET(listener, &read_fds); // also listen for new connections
        if (listener > fdmax) fdmax = listener; 

        // LOG(Level::Debug) << "fdmax: " << fdmax << endl;
        int rv = select(fdmax+1, &read_fds, &write_fds, NULL, NULL);
        // LOG(Level::Debug) << "select: " << rv << endl
            // << "session_set.size() in select: " << session_set.size() << endl;
        switch (rv) {
            case -1:
                LERR << "Select in main loop\n";
                break;
            case 0:
                // TODO: remove sockets that haven't responded in certain amount of time, exept for listener socket
                break;
            default:
                // firstly, iterate through map and process clients in session 
                for (auto &el : session_set) {
                    if (FD_ISSET(el.socket_fd, &read_fds)) {
                        bool recv = try_recv(el) == StatusCode::OK;
                        int buffer_size = el.recv_buffer.size();
                        int packet_size = el.recv_buffer.current_packet_size();
                        cout << "ywx try_recv(el):" << boolalpha << recv 
                            << "el.recv_buffer.size(): " << buffer_size << endl 
                            << "el.recv_buffer.current_packet_size(): " << packet_size << endl;
                        if (recv && buffer_size >= packet_size) {
                            LOG(Level::Debug) << "Info buffer " << el.recv_buffer.size() << endl;
                            LOG(Level::Debug) << "Should be username " << el.recv_buffer.data + 3 << endl;
                            if(PreLayerInstance.fsm(el) == false) {
                                remove_client(el);
                            }
                        }
                        // else if(el.is_scr == true && (buffer_size >= el.scr_packet_size)) {
                        //     LOG(Level::Debug) << "Info buffer " << el.recv_buffer.size() << endl;
                        //     LOG(Level::Debug) << "Should be username " << el.recv_buffer.data + 3 << endl;
                        //     if(PreLayerInstance.fsm(el) == false) {
                        //         remove_client(el);
                        //     }
                        // } 
                        else {
                            remove_client(el);
                            break;
                        }
                    }
                    
                    // cout << "send buffer transport " << el.send_buffer.size() << endl;
                    if (FD_ISSET(el.socket_fd, &write_fds)) {
                        if (try_send(el) != StatusCode::OK) {
                            // remove client
                            cout << "remove_client(el) 3\n";
                            remove_client(el);
                        }
                    }
                }

                // and lastly, check for new connections 
                if (FD_ISSET(listener, &read_fds)) {
                    LOG(Level::Debug) << "listener?ɶ?\n";
                    accept_new_client(listener);
                }
                
                break;
        } // end of switch

        for(auto &el: session_set) {
            if(el.state == SessionState::Acceptance) {
                // send first packet
                PreLayerInstance.fsm(el);
            }
            if(el.is_scr == true) PreLayerInstance.fsm(el);
        }


    } // end of main loop
}

int TransferLayer::reset_rw_fd_sets(fd_set &read_fds, fd_set &write_fds) {
    int maxfd = 0;
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    for (const Client &client : session_set) {
        // set read_fds if have enough buffer size to receive at least the header
        if (client.recv_buffer.get_num_free_bytes() > kHeaderSize) {
            FD_SET(client.socket_fd, &read_fds);
            maxfd = max(maxfd, client.socket_fd);
            // LOG(Level::Debug) << "read_fds\n";
        }

        // set write_fds if has data in send_buffer
        // if (!client.send_buffer.empty()) {
        FD_SET(client.socket_fd, &write_fds);
        maxfd = max(maxfd, client.socket_fd);
        // LOG(Level::Debug) << "write_fds\n";
        // }
    }

    return maxfd;
}

// helper function
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int TransferLayer::accept_new_client(int listener) {
    struct sockaddr_storage remoteaddr; // client address
    socklen_t addrlen = sizeof(remoteaddr);

    int newfd = accept(listener, (sockaddr *) &remoteaddr, &addrlen);
    
    if (newfd == -1) {
        LERR << "accept\n";
        return -1;
    } else {
        // set non-blocking connection
        int val = fcntl(newfd, F_GETFL, 0);
        if (val < 0) {
            close(newfd);
            LERR << "fcntl, GETFL\n";
        }
        if (fcntl(newfd, F_SETFL, val|O_NONBLOCK) < 0) {
            close(newfd);
            LERR << "fcntl, SETFL\n";
        }

        session_set.emplace_back(newfd, kRecvBufferSize);

        char remoteIP[INET6_ADDRSTRLEN];
        std::cout << "?????????ӣ?IP=" << inet_ntop(remoteaddr.ss_family,
                get_in_addr((struct sockaddr*) &remoteaddr),
                remoteIP, INET6_ADDRSTRLEN)
            << "??socket=" << newfd << std::endl;
        
        session_set.back().ipaddr = inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr*) &remoteaddr), remoteIP, INET6_ADDRSTRLEN);
    }
    return newfd;
}

int TransferLayer::get_listener(const short port) {
    // AF_INET: IPv4 protocol
    // SOCK_STREAM: TCP protocol
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        LERR << "Server socket init error" << endl;
        graceful_return("socket", -1);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LERR << "Server setsockopt error" << endl;
        graceful_return("setsockopt", -1);
    }

    int flags = fcntl(server_fd, F_GETFL, 0);
    fcntl(server_fd, F_SETFL, flags|O_NONBLOCK);

    struct sockaddr_in server_addr; 
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port); 
    int server_addrlen = sizeof(server_addr);
    if (bind(server_fd, (struct sockaddr *) &server_addr, server_addrlen) < 0) {
        LERR << "Server bind error" << endl;
        graceful_return("bind", -1);
    }

    if (listen(server_fd, 10) < 0) {
        LERR << "Server listen error" << endl;
        graceful_return("listen", -1); 
    }
    // LOG(Info) << "Server socket init ok with port: " << port << endl;
    // LOG(Info) << "server_fd: " << server_fd<< endl;
    return server_fd;
}

StatusCode TransferLayer::remove_client(Client &client) {
    session_set.remove_if([client](const Client &el){ return el.socket_fd == client.socket_fd; });
    // LOG(Info) << "Client " << client.client_id << " closed connection\n";
    
    return StatusCode::OK;
}