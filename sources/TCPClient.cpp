#include "TCPClient.h"

int TCPClientLib::TCPClient::addReceiveListener(function<void(TCPClientLib::TCPClient *client, char* data,  size_t size)> onReceive)
{
	int id = listenersIdCounter++;
	this->receiveListeners[id] = onReceive;
	return id;
}


int TCPClientLib::TCPClient::addReceiveListener_s(function<void(TCPClientLib::TCPClient *client, string data)> onReceive)
{
	int id = listenersIdCounter++;
	this->receiveListeners_s[id] = onReceive;
	return id;
}

int TCPClientLib::TCPClient::addConEventListener(function<void(TCPClientLib::TCPClient *client, CONN_EVENT event)> onConEvent)
{
	int id = listenersIdCounter++;
	this->connEventsListeners[id] = onConEvent;
	return id;
}


void TCPClientLib::TCPClient::removeListener(int id)
{
	if (this->receiveListeners.count(id))
		this->receiveListeners.erase(id);
}

void TCPClientLib::TCPClient::removeListener_s(int id)
{
	if (this->receiveListeners_s.count(id))
		this->receiveListeners_s.erase(id);
}

void TCPClientLib::TCPClient::removeConEventListener(int id)
{
	if (this->connEventsListeners.count(id))
		this->connEventsListeners.erase(id);
}


void TCPClientLib::TCPClient::notifyListeners_dataReceived(char* data, size_t size)
{
	string dataAsString = "";
	if ((this->receiveListeners_s.size() > 0) || (this->receiveListeners_s.size() > 0))
	{
		dataAsString.resize(size);
		for (size_t i = 0; i < size; i++)
			dataAsString[i] = data[i];
	}

	//notify the events in the TCPServer
	for (auto &c: this->receiveListeners)
	{
		c.second(this, data, size);
	}

	for (auto &c: this->receiveListeners_s)
	{
		c.second(this, dataAsString);
	}

	
	//dataAsString.resize(0);
	dataAsString = "";
}

void TCPClientLib::TCPClient::notifyListeners_connEvent(CONN_EVENT action)
{
	for (auto &c: this->connEventsListeners)
	{
		c.second(this, action);
	}
}

future<bool> TCPClientLib::TCPClient::connectToServer(string server_, int port_)
{
	//create an socket to await for connections

	promise<bool> *prom_ = new promise<bool>();

	
	//std::async(std::launch::async, [&](string server, int port, promise<bool> *prom){
	thread th([&](string server, int port, promise<bool> *prom){

		socketHandle = socket(AF_INET, SOCK_STREAM, 0);
		if (socketHandle >= 0)
		{
			struct sockaddr_in serv_addr;
			
			serv_addr.sin_family = AF_INET;
			serv_addr.sin_port = htons(port);

			if (inet_pton(AF_INET, server.c_str(), &serv_addr.sin_addr) > 0) 
			{
				int cli_fd;
				if ((cli_fd = connect(socketHandle, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) >= 0) 
				{
					waitUntilDisconnectMutex.lock();
					prom->set_value(true);
					this->notifyListeners_connEvent(CONN_EVENT::CONNECTED);
					int bufferSize = _CONF_READ_BUFFER_SIZE;

					char readBuffer[bufferSize]; //10k buffer
					this->running = true;
					int nextLoopWait = _CONF_DEFAULT_LOOP_WAIT;

					while (this->running)
					{
						if (this->SocketIsConnected(socketHandle))
						{
							int availableBytes = 0;
							ioctl(socketHandle, FIONREAD, &availableBytes);

							availableBytes = availableBytes > bufferSize ? bufferSize : availableBytes;

							auto readCount = recv(socketHandle,readBuffer, availableBytes, 0);
							if (readCount > 0)
							{
								this->notifyListeners_dataReceived(readBuffer, readCount);
							}

							if (nextLoopWait > 0)
								usleep(nextLoopWait);

							nextLoopWait = _CONF_DEFAULT_LOOP_WAIT;
						}
						else
						{
							this->running = false;
							//notify disconnection
							this->notifyListeners_connEvent(CONN_EVENT::DISCONNECTED);
						}
					}

					if (isConnected())
						this->disconnect();

					waitUntilDisconnectMutex.unlock();
					
				}
				else
				{
					this->debug("Connection failed");
					prom->set_value(false);
					this->notifyListeners_connEvent(CONN_EVENT::DISCONNECTED);
				}
			}
			else
			{
				this->debug("Invalid address");
				prom->set_value(false);
				this->notifyListeners_connEvent(CONN_EVENT::DISCONNECTED);
			}
		}
		else
		{
			this->debug("General error opening socket");
			prom->set_value(false);
			this->notifyListeners_connEvent(CONN_EVENT::DISCONNECTED);
		}

		delete prom;
	}, server_, port_, prom_);

	th.detach();

	return prom_->get_future();
}

		

bool TCPClientLib::TCPClient::SocketIsConnected(int socket)
{
	char data;
	int readed = recv(socket,&data,1, MSG_PEEK | MSG_DONTWAIT);//read one byte (but not consume this)

	int error_code;
	socklen_t error_code_size = sizeof(error_code);
	auto getsockoptRet = getsockopt(socket, SOL_SOCKET, SO_ERROR, &error_code, &error_code_size);
	//string desc(strerror(error_code));
	//return error_code == 0;

	//in the ser of "'TCPCLientLib", after tests, I received 0 in the var 'readed' when server closes the connection and error_code is always 0, wheter or not connected to the server
	//in the case of "TCPServerLib", the error_code works fine

	if (readed == 0)
		return false;
	

	if (getsockoptRet < 0) {
		return false;
	} else if (error_code == 0) {
		return true;
	} else {
		return false;
	}
}


void TCPClientLib::TCPClient::sendData(char* data, size_t size)
{ 
	writeMutex.lock();

	if (SocketIsConnected(socketHandle))
	{
		auto bytesWrite = send(socketHandle, data, size, 0);
	}
	else
		this->debug("Try sendind data to disconnected client");
	
	writeMutex.unlock();
}

void TCPClientLib::TCPClient::sendString(string data)
{
	this->sendData((char*)data.c_str(), data.size());
}

	
void TCPClientLib::TCPClient::disconnect()
{
	this->running = false;
	close(socketHandle);
}

bool TCPClientLib::TCPClient::isConnected()
{
	return this->SocketIsConnected(socketHandle);
}

bool TCPClientLib::TCPClient::SetSocketBlockingEnabled(int fd, bool blocking)
{
	if (fd < 0) return false;

	#ifdef _WIN32
		unsigned long mode = blocking ? 0 : 1;
		return (ioctlsocket(fd, FIONBIO, &mode) == 0) ? true : false;
	#else
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags < 0) return false;
		flags = blocking ? (flags&~O_NONBLOCK) : (flags|O_NONBLOCK);
		return (fcntl(fd, F_SETFL, flags) == 0) ? true : false;
	#endif
}

future<void> TCPClientLib::TCPClient::waitUntilDisconnect()
{
	shared_ptr<promise<void>> prom = shared_ptr<promise<void>>(new promise<void>);
	thread th([&](shared_ptr<promise<void>> prom_){
		waitUntilDisconnectMutex.lock();
		waitUntilDisconnectMutex.unlock();
		prom_->set_value();
	}, prom);
	th.detach();

	return prom->get_future();
}
