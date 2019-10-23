namespace AmazingRPG
{
    // per player data
    struct PlayerDesc
    {
        std::string id;
        int level{ 1 };
        int strength{ 0 };
        int intellect{ 0 };
    
        std::string GetString()
        {
            std::stringstream sstrm;
            sstrm << "Player ID: " << id << std::endl;
            sstrm << "\tLevel: " << level << std::endl;
            sstrm << "\tStrength: " << strength << std::endl;
            sstrm << "\tIntellect: " << intellect << std::endl;
            return sstrm.str();
        }
    };

    // shared socket settings
    const u_short PORT{ 27015 };
    const size_t SOCKET_BUFFER_SIZE = 8192;

    // size of the ID strings
    const int ID_SIZE{ 30 };

    // protocol
    // all control codes will be followed by 30 characters of ID
    // for the demo the client doesn't send any integers for the stats
    // to keep the protocol simple
    const char VIEW{ 'V' }; // view player info
    const char STR{ 'S' };  // increment strength by 1
    const char INT{ 'I' };  // increment intellect by 1

    inline std::string GetPlayerIDForInt(int id)
    {
        std::stringstream name;
        name << std::setfill('0') << std::setw(ID_SIZE) << id;
        return name.str();
    }

    inline std::string AskForPlayerID()
    {
        std::cout << "Type the player ID as a positive integer: ";
        int id{ 0 };
        std::cin >> id;
        if (std::cin.fail() || id < 0)
        {
            id = 0;
            std::cout << "You didn't enter a positive integer" << std::endl;
        }

        return GetPlayerIDForInt(id);
    }
}