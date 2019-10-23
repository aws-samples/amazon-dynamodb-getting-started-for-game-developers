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

// Standard library
#include <iostream>
#include <iomanip>
#include <thread>
#include <random>
#include <cmath>
#include <list>

// AWS C++ SDK
#include <aws/core/Aws.h>
#include <aws/core/utils/logging/ConsoleLogSystem.h>
#include <aws/core/utils/logging/AWSLogging.h>
#include <aws/core/utils/Outcome.h> 
#include <aws/dynamodb/DynamoDBClient.h>
#include <aws/dynamodb/model/AttributeDefinition.h>
#include <aws/dynamodb/model/BatchWriteItemRequest.h>
#include <aws/dynamodb/model/BatchWriteItemResult.h>
#include <aws/dynamodb/model/DescribeTableRequest.h>
#include <aws/dynamodb/model/DescribeTableResult.h>
#include <aws/dynamodb/model/QueryRequest.h>
#include <aws/dynamodb/model/QueryResult.h>
#include <aws/dynamodb/model/ScanRequest.h>
#include <aws/dynamodb/model/ScanResult.h>
#include <aws/dynamodb/model/UpdateItemRequest.h>
#include <aws/dynamodb/model/UpdateItemResult.h>

// Project includes
#include "Settings.h"
#include "..\Common\common.h"

using namespace std;

namespace AmazingRPG
{
    //////////////////////////////////////////////////////////////////////////////
    // Store socket info
    struct SocketInformation {
        WSABUF dataBuffer{};
        SOCKET socket{INVALID_SOCKET};
        OVERLAPPED overlapped{};
        char writeBuffer[SOCKET_BUFFER_SIZE];
        int bytesSEND{ 0 };
        char readBuffer[SOCKET_BUFFER_SIZE];
        int bytesRECV{ 0 };
    };

    //////////////////////////////////////////////////////////////////////////////
    // AWS client statics
    static shared_ptr<Aws::DynamoDB::DynamoDBClient> s_DynamoDBClient;
    const size_t MAX_DYNAMODB_BATCH_ITEMS{ 25 };

    //////////////////////////////////////////////////////////////////////////////
    // Game specific statics and constants
    static random_device s_randomDevice{};
    static mt19937 s_randomGenerator{ s_randomDevice() };
	static uniform_int_distribution<> s_levelDistribution{1, 60};
    // our population will be decidedly normal, with some outliers
    // (min and max are 3 standard deviations from average)
    const double MIN_ATTR{ 3.0 };
    const double MAX_ATTR{ 18.0f };
    static normal_distribution<> s_attributeDistribution{ (MAX_ATTR + MIN_ATTR) / 2.0f, (MAX_ATTR - MIN_ATTR) / 6.0f };

    //////////////////////////////////////////////////////////////////////////////
    // data keys
    // When naming your keys, be careful of reserved words https://docs.aws.amazon.com/amazondynamodb/latest/developerguide/ReservedWords.html
    const string PLAYER_DATA_TABLE_NAME{ "PlayerData" };
    const string DATA_KEY_ID{ "PlayerID" };
    const string DATA_KEY_LEVEL{ "PlayerLevel" };
    const string DATA_KEY_STRENGTH{ "PlayerStrength" };
    const string DATA_KEY_INTELLECT{ "PlayerIntellect" };

    //////////////////////////////////////////////////////////////////////////////
    // Game code
    int GenerateRandomStat()
    {
        // since it's a normal distribution we don't want to clamp, we want
        // to actually discard values that lie outside of our max and min
        // otherwise there could be extra values at max and min
        while (true)
        {
            double testAttr{ s_attributeDistribution(s_randomGenerator) };
            if (testAttr >= MIN_ATTR && testAttr <= MAX_ATTR)
            {
                return lround(testAttr);
            }
        }
    }

    bool GetPlayerDesc(const string& ID, PlayerDesc& playerDesc)
    {
        // first grab player attributes
        Aws::DynamoDB::Model::QueryRequest queryRequest;
        queryRequest.SetTableName(PLAYER_DATA_TABLE_NAME);
        string conditionExpression{ DATA_KEY_ID };
        conditionExpression += " = :id";
        queryRequest.SetKeyConditionExpression(conditionExpression); //https://docs.aws.amazon.com/amazondynamodb/latest/developerguide/Query.html
        Aws::DynamoDB::Model::AttributeValue avID;
        avID.SetS(ID);
        map<string, Aws::DynamoDB::Model::AttributeValue> attributeValues;
        attributeValues[":id"] = avID;
        queryRequest.SetExpressionAttributeValues(attributeValues);
        auto outcome{ s_DynamoDBClient->Query(queryRequest) };
        if (outcome.IsSuccess())
        {
            auto result{ outcome.GetResult() };
            if (result.GetItems().size() == 0)
            {
                cout << "No player description returned for ID " << ID << endl;
                return false;
            }

            if (result.GetItems().size() > 1)
            {
                cout << "Multiple records found for ID " << ID << " returning first found only!" << endl;
            }

            auto item = result.GetItems()[0];
            playerDesc.id = item[DATA_KEY_ID].GetS();   // we already know this, just showing how to read it
            playerDesc.strength = stoi(item[DATA_KEY_STRENGTH].GetN());
            playerDesc.intellect = stoi(item[DATA_KEY_INTELLECT].GetN());
        }
        else
        {
            cout << "Error querying DynamoDB: " << outcome.GetError() << endl;
            return false;
        }

        return true;
    }
    int AskForNewAttributeValue(const string& attributeText)
    {
        cout << "Type the new "<< attributeText << " as a positive integer:  ";
        int level{ 0 };
        cin >> level;
        if (cin.fail() || level < 0)
        {
            level = 0;
            cout << "You didn't enter a positive integer" << endl;
        }
        return level;
    }

    string FetchPlayerDescAsString(const string& ID)
    {
        PlayerDesc playerDesc;
        if (GetPlayerDesc(ID, playerDesc))
        {
            return playerDesc.GetString();
        }
        return {};
    }
    
    void ViewPlayer(const string& ID)
    {
        PlayerDesc playerDesc;
        if (GetPlayerDesc(ID, playerDesc))
        {
            cout << playerDesc.GetString();
        }
        else
        {
            cout << "Player " << ID << " not found!" << endl;
        }
    }

    bool SetPlayerAttribueValue(const string& ID, const string& attributeKey, int newValue)
    {
        Aws::DynamoDB::Model::UpdateItemRequest updateItemRequest;
        updateItemRequest.SetTableName(PLAYER_DATA_TABLE_NAME);

        // It's worth noting that the current AWS C++ SDK example for upating an
        // item is incorrect, AttributeUpdates are no longer used, you need
        // to use update expressions instead: https://docs.aws.amazon.com/amazondynamodb/latest/developerguide/Expressions.UpdateExpressions.html
        Aws::DynamoDB::Model::AttributeValue avID;
        avID.SetS(ID);
        updateItemRequest.AddKey(DATA_KEY_ID, avID);

        string updateExpression = "SET ";
        updateExpression += attributeKey;
        updateExpression += " = :l";
        updateItemRequest.SetUpdateExpression(updateExpression);
        
        Aws::DynamoDB::Model::AttributeValue av;
        av.SetN(to_string(newValue));
        map<string, Aws::DynamoDB::Model::AttributeValue> attributeValues;
        attributeValues[":l"] = av;
        updateItemRequest.SetExpressionAttributeValues(attributeValues);

        auto outcome{ s_DynamoDBClient->UpdateItem(updateItemRequest) };
        if (outcome.IsSuccess())
        {
            cout << "Player attribute " << attributeKey << " successfully updated" << endl;
            return true;
        }
        else
        {
            cout << "Update player attribute " << attributeKey << " failed: " << outcome.GetError() << endl;
            return false;
        }
    }

    bool PlayerMenu()
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

        switch (menuSelection)
        {
        case 1:
            {
                auto ID = AskForPlayerID();
                ViewPlayer(ID);
                break;
            }

        case 2:
            {
                auto ID = AskForPlayerID();
                auto newValue = AskForNewAttributeValue("strength");
                if (newValue > 0)
                {
                    SetPlayerAttribueValue(ID, DATA_KEY_STRENGTH, newValue);
                }
                else
                {
                    cout << "No valid value to set, ignoring" << endl;
                }
                break;
            }

        case 3:
        {
            auto ID = AskForPlayerID();
            auto newValue = AskForNewAttributeValue("intellect");
            if (newValue > 0)
            {
                SetPlayerAttribueValue(ID, DATA_KEY_INTELLECT, newValue);
            }
            else
            {
                cout << "No valid value to set, ignoring" << endl;
            }
            break;
        }

        case 9:
            return false;

        default:
            cout << "That choice doesn't exist, please try again." << endl << endl;
        }
        return true;
    }

    void EmulatePlayerMenu()
    {
        while (PlayerMenu())
        {
            std::this_thread::sleep_for(std::chrono::seconds(0));
        }
    }

    void ShowTopTenPlayers()
    {
    }

	void SendPlayerChunkToDynamoDB(const vector<PlayerDesc>& playerChunk)
	{
		assert(playerChunk.size() <= MAX_DYNAMODB_BATCH_ITEMS);
		vector<Aws::DynamoDB::Model::WriteRequest> writeRequests;
		for (const auto& chunkItem : playerChunk)
		{
			Aws::DynamoDB::Model::AttributeValue avID;
			avID.SetS(chunkItem.id);
			Aws::DynamoDB::Model::AttributeValue avStrength;
			avStrength.SetN(to_string(chunkItem.strength));
			Aws::DynamoDB::Model::AttributeValue avIntellect;
			avIntellect.SetN(to_string(chunkItem.intellect));

			Aws::DynamoDB::Model::PutRequest putRequest;
			putRequest.AddItem(DATA_KEY_ID, avID);
			putRequest.AddItem(DATA_KEY_STRENGTH, avStrength);
			putRequest.AddItem(DATA_KEY_INTELLECT, avIntellect);

			Aws::DynamoDB::Model::WriteRequest curWriteRequest;
			curWriteRequest.SetPutRequest(putRequest);
			writeRequests.push_back(curWriteRequest);
		}

		Aws::DynamoDB::Model::BatchWriteItemRequest batchWriteRequest;
		batchWriteRequest.AddRequestItems(PLAYER_DATA_TABLE_NAME, writeRequests);

        auto outcome{ s_DynamoDBClient->BatchWriteItem(batchWriteRequest) };
		if (outcome.IsSuccess())
		{
            auto result{ outcome.GetResult() };
			if (result.GetUnprocessedItems().size() > 0)
			{
                // if there are unprocessed items, you'd typically do a retry here
                // with exponential falloff, however for this small demo we won't
                // get more than one call to DyanamoDB so it's not needed
            }
			else
			{
				cout << "Chunk successfully sent to DynamoDB!" << endl;
			}
		}
		else
		{
			cout << "Unable to process batch write request: " << outcome.GetError() << endl;
		}
	}

	// This is here to show how to use the DescribeTable and BatchWrite operations
	// however, I would strongly recommend tools like this be written as
	// AWS Lambdas if possible. Your server and even your custom tools
	// shouldn't do this type of thing as it is a waste of your
	// bandwidth allowance and would generally be cheaper to run in AWS
	bool PopulateDatabases()
    {
        // Check that DynamoDB table isn't already populated
		// we don't want to add a bunch of entries accidentally
        PlayerDesc _unused;
        if (GetPlayerDesc(GetPlayerIDForInt(1), _unused))
        {
            cout << "The database is already populated, exiting" << endl;
            return false;
        }

		cout << "This could take many minutes, please be patient" << endl;

        // Create a bunch of random characters
        const int NUMBER_OF_CHARACTERS_TO_CREATE{ 1000 };
		vector<PlayerDesc> newPlayerChunk;
		for (int chrIdx{ 0 }; chrIdx < NUMBER_OF_CHARACTERS_TO_CREATE; ++chrIdx)
		{
            PlayerDesc newPlayer;
            newPlayer.id = GetPlayerIDForInt(chrIdx);
            newPlayer.level = s_levelDistribution(s_randomGenerator);
            newPlayer.strength = GenerateRandomStat();
			newPlayer.intellect = GenerateRandomStat();

			// Write the characters to DynamoDB
			// DynamoDB can write up to 16MB of data in one shot but only process 25 items in one
			// request, so we'll chunk through every 25
			newPlayerChunk.push_back(newPlayer);
			if (newPlayerChunk.size() == MAX_DYNAMODB_BATCH_ITEMS)
			{
				cout << "Sending player chunk to DynamoDB..." << endl;
				SendPlayerChunkToDynamoDB(newPlayerChunk);
                newPlayerChunk.clear();
            }
        }


		return true;
    }

    void CopyStringToWriteBuffer(const string& message, SocketInformation& socketInfo)
    {
        copy(message.begin(), message.end(), socketInfo.writeBuffer);
        socketInfo.bytesSEND = static_cast<int>(message.size());
    }

    void ProcessSocket(SocketInformation& socketInfo)
    {
        if (socketInfo.bytesRECV > 0)
        {
            // demo is very limited in its socket abilites as we want to show the database, not how to make a socket server :)
            if (socketInfo.bytesRECV != ID_SIZE + 1)
            {
                if (socketInfo.bytesRECV < ID_SIZE + 1)
                {
                    cout << "Socket receieved less data than expected, sending error to user" << endl;
                    CopyStringToWriteBuffer("Socket receieved less data than expected, sending error to user", socketInfo);
                    return;
                }
                else
                {
                    cout << "Socket received more data than expected, only attempting to process 1 data item" << endl;
                }
            }

            // extract user ID
            string playerID{ socketInfo.readBuffer + 1, static_cast<size_t>(ID_SIZE) };

            // extract control code
            char controlCode{ socketInfo.readBuffer[0] };

            if(controlCode == VIEW)
            {
                string playerDescString{ FetchPlayerDescAsString(playerID) };
                if(playerDescString.empty())
                {
                    CopyStringToWriteBuffer("Unable to find player ID " + playerID, socketInfo);
                }
                else
                {
                    CopyStringToWriteBuffer(playerDescString, socketInfo);
                }
            }
            else if(controlCode == STR || controlCode == INT)
            {
                PlayerDesc playerDesc;
                if (GetPlayerDesc(playerID, playerDesc))
                {
                    int attrValue;
                    string attrKey;
                    if (controlCode == STR)
                    {
                        attrValue = playerDesc.strength;
                        attrKey = DATA_KEY_STRENGTH;
                    }
                    else
                    {
                        attrValue = playerDesc.intellect;
                        attrKey = DATA_KEY_INTELLECT;
                    }
                    ++attrValue;    // demo just adjusts by 1
                    if (SetPlayerAttribueValue(playerID, attrKey, attrValue))
                    {
                        stringstream outstr;
                        outstr << "Attribute " << attrKey << " increased to " << attrValue;
                        CopyStringToWriteBuffer(outstr.str(), socketInfo);
                    }
                    else
                    {
                        CopyStringToWriteBuffer("Unable to adjust player attribute value for " + attrKey, socketInfo);
                    }
                }
                else
                {
                    CopyStringToWriteBuffer("Unable to find player ID " + playerID, socketInfo);
                }
            }
            else
            {
                CopyStringToWriteBuffer("Invalid control code sent to server", socketInfo);
            }
        }
    }

    void FreeSockets(vector<SocketInformation>& socketList, const vector<SocketInformation>& socketsToFree)
    {
        for (const SocketInformation& socketToFree : socketsToFree)
        {
            socketList.erase(std::remove_if(socketList.begin(), socketList.end(), [socketToFree](const SocketInformation& info) {
                return info.socket == socketToFree.socket;
            }), socketList.end());
        }
    }

    bool RunSocketServerLoop()
    {
        cout << "Starting socket server" << endl;
        WSADATA wsaData;
        int errorNum{ WSAStartup(MAKEWORD(2, 2), &wsaData) };
        if (errorNum != 0)
        {
            std::cout << "WSAStartup failed with error " << errorNum << std::endl;
            return true;
        }

        SOCKET listenSocket{ WSASocket(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED) };
        if (listenSocket == INVALID_SOCKET)
        {
            if (listenSocket == INVALID_SOCKET)
            {
                std::cout << "Socket failed with error " << WSAGetLastError() << std::endl;
                WSACleanup();
                return true;
            }
        }

        SOCKADDR_IN internetAddr;
        internetAddr.sin_family = AF_INET;
        internetAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        internetAddr.sin_port = htons(PORT);

        if (::bind(listenSocket, reinterpret_cast<LPSOCKADDR>(&internetAddr), static_cast<int>(sizeof(internetAddr))) == SOCKET_ERROR)
        {
            std::cout << "Socket bind failed with error " << WSAGetLastError() << std::endl;
            closesocket(listenSocket);
            WSACleanup();
            return true;
        }

        // don't need a particularly big queue for pending connections in this demo, so we pick size of 20
        if (listen(listenSocket, 20) == SOCKET_ERROR)
        {
            std::cout << "Listen failed with error " << WSAGetLastError() << std::endl;
            closesocket(listenSocket);
            WSACleanup();
            return true;
        }

        ULONG nonBlock{ 1 };
        if (ioctlsocket(listenSocket, FIONBIO, &nonBlock) == SOCKET_ERROR)
        {
            std::cout << "Unable to set socket FIONBIO parameter due to error " << WSAGetLastError() << std::endl;
            closesocket(listenSocket);
            WSACleanup();
            return true;
        }

        cout << "Listening on port " + PORT << endl;

        bool running = true;
        SOCKET acceptSocket;
        FD_SET writeSet;
        FD_SET readSet;
        int total;
        DWORD flags;
        DWORD sendBytes;
        DWORD recvBytes;
        vector<SocketInformation> socketList;

        while (running)
        {
            Sleep(500);

            ZeroMemory(&readSet, sizeof(readSet));
            ZeroMemory(&writeSet, sizeof(writeSet));

            FD_SET(listenSocket, &readSet);

            for (const SocketInformation& socketInfo : socketList)
            {
                FD_SET(socketInfo.socket, &writeSet);
                FD_SET(socketInfo.socket, &readSet);
            }
            
            total = select(0, &readSet, &writeSet, nullptr, nullptr);
            if (total == SOCKET_ERROR)
            {
                std::cout << "select error " << WSAGetLastError() << std::endl;
                closesocket(listenSocket);
                WSACleanup();
                return true;
            }

            // check for new connections on the listening socket
            if (FD_ISSET(listenSocket, &readSet))
            {
                --total;
                acceptSocket = accept(listenSocket, nullptr, nullptr);
                if (acceptSocket != INVALID_SOCKET)
                {
                    nonBlock = 1;
                    errorNum = ioctlsocket(acceptSocket, FIONBIO, &nonBlock);
                    if (errorNum == SOCKET_ERROR)
                    {
                        std::cout << "couldnt set FIONBTO on acceptSocket due to error " << WSAGetLastError() << std::endl;
                        closesocket(listenSocket);
                        WSACleanup();
                        return true;
                    }
                    SocketInformation socketInfo;
                    socketInfo.socket = acceptSocket;
                    socketList.push_back(socketInfo);
                }
                else
                {
                    if (WSAGetLastError() != WSAEWOULDBLOCK)
                    {
                        std::cout << "accept error " << WSAGetLastError() << std::endl;
                        closesocket(listenSocket);
                        WSACleanup();
                        return true;
                    }
                }
            }

            vector<SocketInformation> socketsToFree;
            // check each socket for a read and write notification until each socket with a notification
            // has been dealt with (total contains the number of sockets with read or write notificatons)
            for (SocketInformation& socketInfo : socketList)
            {
                // read when the read flag is set
                if (FD_ISSET(socketInfo.socket, &readSet))
                {
                    --total;
                    socketInfo.dataBuffer.buf = socketInfo.readBuffer;
                    socketInfo.dataBuffer.len = SOCKET_BUFFER_SIZE;
                    flags = 0;
                    if (WSARecv(socketInfo.socket, &(socketInfo.dataBuffer), 1, &recvBytes, &flags, nullptr, nullptr) == SOCKET_ERROR)
                    {
                        if (WSAGetLastError() != WSAEWOULDBLOCK)
                        {
                            std::cout << "Socket read error, closing socket due to error " << WSAGetLastError() << std::endl;
                            socketsToFree.push_back(socketInfo);
                        }
                    }
                    else
                    {
                        socketInfo.bytesRECV = recvBytes;
                        if (recvBytes == 0)
                        {
                            // zero bytes read indicates client closed connection
                            socketsToFree.push_back(socketInfo);
                        }
                    }
                }

                // if we read something from the socket, act on the read and determine what to write
                ProcessSocket(socketInfo);

                // Anything to write yet?
                if (socketInfo.bytesSEND > 0)
                {
                    if (FD_ISSET(socketInfo.socket, &writeSet))
                    {
                        --total;
                        socketInfo.dataBuffer.buf = socketInfo.writeBuffer;
                        socketInfo.dataBuffer.len = socketInfo.bytesSEND;
                        if (WSASend(socketInfo.socket, &(socketInfo.dataBuffer), 1, &sendBytes, 0, nullptr, nullptr) == SOCKET_ERROR)
                        {
                            if (WSAGetLastError() != WSAEWOULDBLOCK)
                            {
                                std::cout << "Socket write error, closing socket due to error " << WSAGetLastError() << std::endl;
                                socketsToFree.push_back(socketInfo);
                            }
                            else
                            {
                                socketInfo.bytesSEND = 0;
                            }
                        }
                    }
                }
            }

            FreeSockets(socketList, socketsToFree);
        }

        return true;
    }
    
    bool Menu()
    {
        cout << endl << "What would you like to do?" << endl;
        cout << "\t1. Player info (goes to a new menu)" << endl;
        cout << "\t2. Run socket server loop" << endl;
        cout << "\t7. Populate database with fake players" << endl;
        cout << "\t9. Quit" << endl;
        cout << endl << "Your choice? ";

        int menuSelection{ 0 };
        if (!(cin >> menuSelection))
        {
            cin.clear();
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
        }

        switch (menuSelection)
        {
        case 1:
            EmulatePlayerMenu();
            break;
        
        case 2:
            return RunSocketServerLoop();
        
        case 7:
            PopulateDatabases();
            break;

        case 9:
            return false;

        default:
            cout << "That choice doesn't exist, please try again." << endl << endl;
        }
        return true;
    }

    int RunMainLoop()
    {
        cout << "Welcome to The Game!" << endl;

        while (Menu())
        {
            //TODO check for reads in here
            std::this_thread::sleep_for(std::chrono::seconds(0));
        }
        return 0;
    }
}

int main()
{
    int exitStatus{ 0 };
    Aws::SDKOptions options;

    Aws::Utils::Logging::LogLevel logLevel{ Aws::Utils::Logging::LogLevel::Error };
    options.loggingOptions.logger_create_fn = [logLevel] {return make_shared<Aws::Utils::Logging::ConsoleLogSystem>(logLevel); };

    Aws::InitAPI(options);

    Aws::Client::ClientConfiguration clientConfig;
	clientConfig.region = AmazingRPG::REGION;
    AmazingRPG::s_DynamoDBClient = Aws::MakeShared<Aws::DynamoDB::DynamoDBClient>("DyanmoDBClient", clientConfig);

    exitStatus = AmazingRPG::RunMainLoop();

    Aws::ShutdownAPI(options);
    return exitStatus;
}