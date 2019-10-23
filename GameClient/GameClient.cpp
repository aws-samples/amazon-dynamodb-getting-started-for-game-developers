// Sockets library
#include <WinSock2.h>
#include <WS2tcpip.h>
// windows.h, which is included by WinSock2.h has some defines that
// conflict with the AWS libraries and the standard C++ library
// we don't require these defines, so we'll remove them
#undef min
#undef max
#undef IN
#undef GetMessage

#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>

#include "..\Common\common.h"

using namespace std;

const char SERVERADDR[] = "127.0.0.1";

namespace AmazingRPG
{

    bool RunSocketClient()
    {
        // first grab the player ID
        std::string playerID = AskForPlayerID();
        
        cout << "Attempting to connect to server at " << SERVERADDR << ":" << PORT << endl;

        WSADATA wsaData;
        int errorNum = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (errorNum != 0)
        {
            cout << "WSAStartup failed with error " << errorNum << endl;
            return false;
        }

        addrinfo hints;
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* addrResult = nullptr;
        errorNum = getaddrinfo(SERVERADDR, to_string(PORT).c_str(), &hints, &addrResult);
        if (errorNum != 0)
        {
            cout << "getaddrinfo failed with error " << errorNum << endl;
            WSACleanup();
            return false;
        }

        SOCKET connectSocket = INVALID_SOCKET;
        addrinfo* curAddr = nullptr;
        for (curAddr = addrResult; curAddr != nullptr; curAddr = curAddr->ai_next)
        {
            connectSocket = socket(curAddr->ai_family, curAddr->ai_socktype, curAddr->ai_protocol);
            if (connectSocket == INVALID_SOCKET)
            {
                cout << "Socket failed with error " << WSAGetLastError() << endl;
                WSACleanup();
                return false;
            }

            errorNum = connect(connectSocket, curAddr->ai_addr, (int)curAddr->ai_addrlen);
            // if can't connect to the given addrInfo, try the next one
            if (errorNum == INVALID_SOCKET)
            {
                closesocket(connectSocket);
                connectSocket = INVALID_SOCKET;
                continue;
            }
            break;
        }
        freeaddrinfo(addrResult);

        if (connectSocket == INVALID_SOCKET)
        {
            cout << "Unable to connect with server" << endl;
            WSACleanup();
            return false;
        }

        bool running = true;
        while (running)
        {
            cout << endl << "What would you like to do?" << endl;
            cout << "\t1. View Player" << endl;
            cout << "\t2. Increase player strength" << endl;
            cout << "\t3. Increase player intellect" << endl;
            cout << "\t9. Quit" << endl;
            cout << endl << "Your choice? ";

            int menuSelection{ 0 };
            if (!(cin >> menuSelection))
            {
                cin.clear();
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
            }

            string command;
            switch (menuSelection)
            {
                case 1:
                    command = VIEW;
                    break;
                case 2:
                    command = STR;
                    break;
                case 3:
                    command = INT;
                    break;
                case 9:
                    cout << "Shutting down socket and quitting" << endl;
                    running = false;
                    continue;
                default:
                    cout << "That choice doesn't exist, please try again." << endl << endl;
                    continue;
            }
            
            command += playerID;

            // send key and wait for response
            string accessToken;
            int bytexfer{ send(connectSocket, command.c_str(), static_cast<int>(command.length()), 0) };
            if (bytexfer == SOCKET_ERROR)
            {
                cout << "Send access token failed due to error " << WSAGetLastError() << endl;
                closesocket(connectSocket);
                WSACleanup();
                return false;
            }

            cout << "Token bytes sent: " << bytexfer << endl;

            char recvBuffer[SOCKET_BUFFER_SIZE];

            do
            {
                bytexfer = recv(connectSocket, recvBuffer, SOCKET_BUFFER_SIZE, 0);
                if (bytexfer > 0)
                {
                    cout << "Bytes received: " << bytexfer << endl;
                    cout.write(recvBuffer, bytexfer);
                    cout << endl;

                    std::string serverResponse{ recvBuffer, recvBuffer + bytexfer };
                    cout << "Server response: " << serverResponse << endl;
                    break;
                }
                else if (bytexfer == 0)
                {
                    cout << "Connection closed" << endl;
                }
                else
                {
                    cout << "Error receiving data: " << WSAGetLastError() << endl;
                }

                Sleep(5);
            } while (bytexfer > 0);
        }

        closesocket(connectSocket);
        WSACleanup();

        return true;
    }

}

int main()
{
    AmazingRPG::RunSocketClient();
}
