# Amazon DynamoDB Getting Started For Game Developers
A very basic client/server implementation in C++ showing how game developers can integrate Amazon DynamoDB calls in to their game server.

# Requirements
- An AWS account with access to Amazon DynamoDB and Amazon S3: https://aws.amazon.com/getting-started/
- Microsoft Visual Studio 2017 or higher (any edition): https://visualstudio.microsoft.com/
- The AWS C++ SDK, installation instructions here: https://aws.amazon.com/blogs/gametech/game-developers-guide-to-the-aws-sdk/

# Contents
<pre>
├── GameServer/         # Game server code as well as the solution file for both server and client projects
├── GameClient/         # Game client code 
└── Common/             # Code shared between game client and server
</pre>

# Set up AWS resources
- Create a table in DynamoDB named "PlayerData".
- Set the primary key to "PlayerID" and make sure the data type is "string".
- Otherwise use default settings.

# Build and run the sample
- Add the AWS C++ SDK to your project. The Amazon DynamoDB library is required, as well as its dependencies.
- Open the solution file GameServer.sln, which contains both the server and client projects.
- If you'd created your DynamoDB table in an AWS region other than US East 1, modify the GameServer/Settings.h with the correct region.
- Build the server and client projects.
- The project is currently configured to allow the client to connect to a locally hosted server, so you can run them on the same machine. If you would like to run them on different machines, you can modify the SERVERADDR variable in GameClient.cpp.

# For more information or questions
- The steps in this file are condensed from the article found here: https://aws.amazon.com/blogs/gametech/
- Chat with us on reddit: https://www.reddit.com/r/aws/
- Please contact gametech@amazon.com for any comments or requests regarding this content

# License Summary

This sample code is made available under the MIT-0 license. See the LICENSE file.
