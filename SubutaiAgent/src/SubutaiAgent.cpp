/**
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 *
 *    @copyright 2014 Safehaus.org
 */
/**
 *  @brief     SubutaiAgent.cpp
 *  @class     SubutaiAgent.cpp
 *  @details   This is SubutaiAgent Software main process.
 *  		   It's main responsibility is that send and receive messages from MQTT broker.
 *  		   It also creates a new process using KAThread Class when the new Execute Request comes.
 *  @author    Emin INAL
 *  @author    Bilal BAL
 *  @version   1.1.0
 *  @date      Sep 13, 2014
 */
/** \mainpage  Welcome to Project Subutai Agent
 *	\section   Subutai Agent
 * 			   The Subutai Agent is a simple daemon designed to connect securely to an MQTT server to
 * 			   reliably receive and send messages on queues and topics.
 * 	 	 	   It's purpose is to perform a very simple reduced set of instructions to
 * 	 	 	   manage any system administration task.
 * 	 	 	   The agent may run on physical servers, virtual machines or inside Linux Containers.
 */

#include "SubutaiCommand.h"
#include "SubutaiResponse.h"
#include "SubutaiUserID.h"
#include "SubutaiResponsePack.h"
#include "SubutaiThread.h"
#include "SubutaiConnection.h"
#include "SubutaiLogger.h"
#include "SubutaiWatch.h"
#include "SubutaiEnvironment.h"
#include "SubutaiContainerManager.h"
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <iostream>

/**
 *  \details   This method designed for Typically conversion from integer to string.
 */
string toString(int intcont)
{		//integer to string conversion
    ostringstream dummy;
    dummy << intcont;
    return dummy.str();
}

/**
 *  \details   threadSend function sends string messages in the Shared Memory buffer to MQTT Broker.
 *  		   This is a thread with working concurrently with main thread.
 *  		   It is main purpose that checking the Shared Memory Buffer in Blocking mode and sending them to Broker
 */
void threadSend(message_queue *mq,SubutaiConnection *connection,SubutaiLogger* logMain)
{
    try
    {
        string str;
        unsigned int priority;
        size_t recvd_size;
        while(true)
        {
            str.resize(2500);
            mq->receive(&str[0], str.size(), recvd_size, priority);
            logMain->writeLog(7, logMain->setLogData("<SubutaiAgent>::<threadsend>",
                        "New message comes to messagequeue to be sent:", str));
            connection->sendMessage(str.c_str());
            str.clear();
        }
        message_queue::remove("message_queue");
    }
    catch(interprocess_exception &ex)
    {
        message_queue::remove("message_queue");
        std::cout << ex.what() << std::endl;
        logMain->writeLog(3, logMain->setLogData("<SubutaiAgent>::<threadsend>", "New exception Handled:", ex.what()));
    }
}

/**
 *  \details   This method checks the Default HeartBeat execution timeout value.
 *  		   if execution timeout is occured it returns true. Otherwise it returns false.
 */
bool checkExecutionTimeout(unsigned int* startsec, bool* overflag, unsigned int* exectimeout, unsigned int* count)
{
    if (*exectimeout != 0)
    {
        boost::posix_time::ptime current = boost::posix_time::second_clock::local_time();
        unsigned int currentsec  =  current.time_of_day().seconds();

        if ((currentsec > *startsec) && *overflag==false)
        {
            if (currentsec != 59)
            {
                *count = *count + (currentsec - *startsec);
                *startsec = currentsec;
            }
            else
            {
                *count = *count + (currentsec - *startsec);
                *overflag = true;
                *startsec = 1;
            }
        }
        if (currentsec == 59)
        {
            *overflag = true;
            *startsec = 1;
        }
        else
        {
            *overflag = false;
        }
        if (*count >= *exectimeout)     //timeout
        {
            return true;	        //timeout occured now
        }
        else
        {
            return false;               //no timeout occured
        }
    }
    return false;	                //no timeout occured
}


static void * updateContainerInfo(void * arg )
{
    pthread_detach( pthread_self() ) ;

    SubutaiContainerManager* container_manager = (SubutaiContainerManager*) arg;
    while( true )
    {
        // do background task, i.e.:
        cout << "hello" << endl;
        container_manager->updateContainerLists();
        usleep(3000000) ;
    }
}

int updateContainerInfosPeriodically( SubutaiContainerManager* container_manager)
{
    pthread_t thread ;
    pthread_attr_t attr ;

    pthread_attr_init( & attr ) ;
    pthread_create( & thread, & attr, updateContainerInfo, container_manager ) ;

}



/**
 *  \details   This function is the main thread of SubutaiAgent.
 *  		   It sends and receives messages from MQTT broker.
 *  		   It is also responsible from creation new process.
 */
int main(int argc,char *argv[],char *envp[])
{
    string serverAddress = "SERVICE_TOPIC";             //Default SERVICE TOPIC
    string broadcastAddress = "BROADCAST_TOPIC";	//Default BROADCAST TOPIC
    string clientAddress;
    SubutaiThread thread;
    SubutaiLogger logMain;
    SubutaiCommand command;
    SubutaiResponsePack response;
    SubutaiEnvironment environment(&logMain);
    string input = "";
    string sendout;

    if (!thread.getUserID().checkRootUser())
    {
        //user is not root SubutaiAgent Will be closed
        cout << "Main Process User is not root.. Subutai Agent is going to be closed.." << endl;
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        return 100;
    }

    if (!logMain.openLogFileWithName("subutai-agent.log"))
    {
        cout << "/var/log/subutai-agent/ folder does not exist.. Subutai Agent is going to be closed.."<<endl;
        FILE* dumplog = fopen("/etc/subutai-agent_dump.log","a+");
        string log = "<DEBUG> /var/log/subutai-agent/ folder does not exist.. "
            "Subutai Agent is going to be closed.. \n";
        fputs(log.c_str(), dumplog);
        fflush(dumplog);
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        return 200;
    }

    logMain.setLogLevel(7);
    logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>", "Subutai Agent is starting.."));
    logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>", "Agent.xml is reading.."));

    /*
     * Getting Environment Settings..
     */

    environment.getAgentSettings();
    environment.getAgentUuid();
    environment.getAgentMacAddress();
    environment.isAgentLxc();
    environment.getAgentIpAddress();
    environment.getAgentParentHostname();
    environment.getAgentEnvironmentId();
    clientAddress = environment.getAgentUuidValue();

    /*
     * Starting Container Manager
     */
    SubutaiContainerManager cman("/var/lib/lxc", &logMain);
    updateContainerInfosPeriodically(&cman);
    cman.write();

    while(true)
    {
    	cout<< "hi" << endl;
    	usleep(5000000);
    }

    /*
     * Opening MQTT Connection
     */

    class SubutaiConnection *connection;
    int rc;
    mosqpp::lib_init();
    connection = new SubutaiConnection(clientAddress.c_str(),
            clientAddress.c_str(),
            serverAddress.c_str(),
            broadcastAddress.c_str(),
            environment.getAgentConnectionUrlValue().c_str(),atoi(environment.getAgentConnectionPortValue().c_str()));

    logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>", "Trying to open Connection with MQTT Broker: ",
                environment.getAgentConnectionUrlValue(), " Port: ", environment.getAgentConnectionPortValue()));

    int reconnectDelay = atoi(environment.getAgentConnectionOptionsValue().c_str()) - 4;

    if (reconnectDelay <= 0)
    {
        logMain.writeLog(3, logMain.setLogData("<SubutaiAgent>", "Reconnect Delay cannot be under 0 !!",
                    " Using default timeout(10).", toString(reconnectDelay)));
        reconnectDelay = 10;
    }

    while (true)
    {
        if (!connection->openSession())
        {
            sleep(reconnectDelay);
            logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>", "Trying connect to MQTT Broker..:", 
                        environment.getAgentConnectionUrlValue()));
            if (connection->reConnect()) {
                break;
            }
        } else {
            break;
        }
    }

    logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>", "Connection Successfully opened with MQTT Broker: ",
                environment.getAgentConnectionUrlValue()));
    logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>", "Registration Message is sending to MQTT Broker.."));

    /*
     * sending registration message
     */

    sendout = response.createRegistrationMessage(
            environment.getAgentUuidValue(), environment.getAgentMacAddressValue(),
            environment.getAgentHostnameValue(),environment.getAgentParentHostnameValue(),
            environment.getAgentEnvironmentIdValue(),environment.getAgentIpValue());
    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>", "Registration Message:", sendout));
    connection->sendMessage(sendout);

    cman.registerAllContainers(connection);

    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Shared Memory MessageQueue is initializing.."));
    message_queue messageQueue
        (open_or_create,        //only create
         "message_queue",       //name
         100,                   //max message number
         2500                   //max message size
        );

    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>", "Sending Thread is starting.."));
    boost::thread thread1(threadSend, &messageQueue, connection, &logMain);
    /* Change the file mode mask */
    umask(0);

    /*
     * initializing timer settings
     */
    boost::posix_time::ptime start =            boost::posix_time::second_clock::local_time();
    boost::posix_time::ptime startQueue =       boost::posix_time::second_clock::local_time();
    unsigned int exectimeout =                  175;    //180 seconds for HeartBeat Default Timeout
    unsigned int queuetimeout =                 30;     //30 seconds for In Queue Default Timeout
    unsigned int startsec  =                    start.time_of_day().seconds();
    unsigned int startsecQueue  =               start.time_of_day().seconds();
    bool overflag =                             false;
    bool overflagQueue =                        false;
    unsigned int count =                        1;
    unsigned int countQueue =                   1;
    list<int> pidList;
    int ncores =                                -1;
    ncores =                                    sysconf(_SC_NPROCESSORS_CONF);
    int currentProcess =                        0;
    string str, str2; 
    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>", "Number of cpu core:", toString(ncores)));
    logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>", "Client Address:", clientAddress));

    /*
     * initializing inotify handler
     */

    SubutaiWatch Watcher(connection, &response, &logMain);
    Watcher.initialize(20000);
    logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>", "The Watcher is initializing.."));
    while(true)
    {
        try
        {
            if (checkExecutionTimeout(&startsec,&overflag,&exectimeout,&count)) //checking Default Timeout
            {
                //timeout occured!!
                response.clear();
                /*
                 * Refresh new agent ip address set for each heartbeat message
                 */
                environment.getAgentIpAddress();
                response.setIps(environment.getAgentIpValue());
                response.setHostname(environment.getAgentHostnameValue());
                response.setMacAddress(environment.getAgentMacAddressValue());
                string resp = response.createHeartBeatMessage(environment.getAgentUuidValue(),
                        command.getRequestSequenceNumber(),
                        environment.getAgentEnvironmentIdValue(),
                        environment.getAgentMacAddressValue(),
                        environment.getAgentHostnameValue(),
                        environment.getAgentParentHostnameValue(),
                        command.getSource(),
                        command.getTaskUuid());
                connection->sendMessage(resp);

                logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>", "HeartBeat Response:", resp));
                start =         boost::posix_time::second_clock::local_time();	//Reset Default Timeout value
                startsec =      start.time_of_day().seconds();
                overflag =      false;
                exectimeout =   175;
                count =         1;
            }
            if (checkExecutionTimeout(&startsecQueue,&overflagQueue,&queuetimeout,&countQueue))
            {   //checking IN_QUEUE Default Timeout
                //timeout occured!!
                response.clear();
                ifstream queueFile("/etc/subutai-agent/commandQueue.txt");
                string queueElement;
                if (queueFile.peek() != ifstream::traits_type::eof())
                {
                    while(getline(queueFile, queueElement))
                    {
                        if (command.deserialize(queueElement))
                        {
                            string resp = response.createInQueueMessage(environment.getAgentUuidValue(), command.getTaskUuid());
                            connection->sendMessage(resp);
                            logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>", "IN_QUEUE Response:", resp));
                        }
                        else
                        {
                            cout << "error!!" <<endl;
                            logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>", "Fetched Element:",queueElement));
                        }
                    }
                }
                queueFile.close();
                startQueue =            boost::posix_time::second_clock::local_time();	//Reset Default Timeout values
                startsecQueue  =        startQueue.time_of_day().seconds();
                overflagQueue =         false;
                queuetimeout =          30;
                countQueue =            1;
            }
            command.clear();
            for (list<int>::iterator iter = pidList.begin(); iter != pidList.end();iter++)
            {
                if (pidList.begin() != pidList.end())
                {
                    int status;
                    pid_t result = waitpid(*iter, &status,WNOHANG);
                    if (result != 0)
                    {
                        iter = pidList.erase(iter);
                        currentProcess--;
                    }
                }
            }

            Watcher.checkNotification(); //checking the watch event status

            rc = connection->loop(1); // checking the status of the new message
            if (rc)
            {
                logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","RC:",toString(rc)));
                connection->reconnect();
            }
            if (connection->checkMessageStatus()) //checking new message arrived ?
            {
                logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","total running process:",toString(currentProcess)));
                connection->resetMessageStatus(); //reseting message status
                input = connection->getMessage(); //fetching message..
                logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Fetched Message:",input));

                if (command.deserialize(input))	 //deserialize the message
                {
                    logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>","New Message is received"));
                    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","New Message:", input));
                    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Command source:", command.getSource()));
                    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Command type:", command.getType()));
                    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Command uuid:", command.getUuid()));
                    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Command TaskUuid:", command.getTaskUuid()));
                    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Command RequestSequenceNumber:",
                                toString(command.getRequestSequenceNumber())));
                    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Command workingDirectory:", command.getWorkingDirectory()));
                    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Command StdOut:", command.getStandardOutput()));
                    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Command stdOutPath:", command.getStandardOutputPath()));
                    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Command StdErr:", command.getStandardError()));
                    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Command stdErrPath:", command.getStandardErrPath()));
                    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Command Program:", command.getProgram()));
                    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Command runAs:", command.getRunAs()));
                    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Command timeout:", toString(command.getTimeout())));
                    // Check if this uuid belongs this FAI or one of child containers
                    bool isLocal = true;
                    SubutaiContainer* target_container = cman.findContainerById(command.getUuid());
                    if (target_container) {
                        isLocal = false;
                    }    
                    if (command.getWatchArguments().size()!=0)
                    {
                        for (unsigned int i=0; i<command.getWatchArguments().size(); i++)
                            logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>", "Command WatchArgs:", command.getWatchArguments()[i]));
                    }

                    if (command.getType()=="REGISTRATION_REQUEST_DONE") //type is registration done
                    {
                        logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Registration is done.."));
                        // agent is registered to server now
                    }
                    else if (command.getType()=="EXECUTE_REQUEST")	//execution request will be executed in other process.
                    {
                        if (isLocal) {
                            fstream file;	//opening uuid.txt
                            file.open("/etc/subutai-agent/commandQueue.txt",fstream::in | fstream::out | fstream::app);
                            file << input;
                            logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Received Message to internal currentProcess!"));
                            file.close();
                        } else {
                            // Need to put a command to queue on container
                        }
                    }
                    else if (command.getType()=="PS_REQUEST")
                    {
                        logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","PS execution operation is starting.."));
                        SubutaiThread* subprocess = new SubutaiThread;
                        subprocess->getLogger().setLogLevel(logMain.getLogLevel());
                        command.setProgram("for i in `ps aux | grep '[s]h -c' | awk -F \" \" '{print $2}'`; do ps aux | grep `pgrep -P $i` | sed '/grep/d' ; done 2> /dev/null");
                        command.setWorkingDirectory("/");
                        subprocess->threadFunction(&messageQueue,&command,argv);
                        delete subprocess;
                    }
                    else if (command.getType()=="HEARTBEAT_REQUEST")
                    {
                        logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Heartbeat message has been taken.."));
                        response.clear();
                        /*
                         * Refresh new agent ip address set for each heartbeat message
                         */
                        if (isLocal) {
                            environment.getAgentIpAddress();
                            response.setIps(environment.getAgentIpValue());
                            response.setHostname(environment.getAgentHostnameValue());
                            response.setParentHostname(environment.getAgentParentHostnameValue());
                            response.setMacAddress(environment.getAgentMacAddressValue());
                            string resp = response.createHeartBeatMessage(environment.getAgentUuidValue(),
                                    command.getRequestSequenceNumber(), environment.getAgentEnvironmentIdValue(),
                                    environment.getAgentMacAddressValue(),environment.getAgentParentHostnameValue(),
                                    environment.getAgentParentHostnameValue(),
                                    command.getSource(),command.getTaskUuid());
                            connection->sendMessage(resp);
                            logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","HeartBeat Response:", resp));
                        } else {
                        }
                    }
                    else if (command.getType() == "TERMINATE_REQUEST")
                    {
                        logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Termination request ID:",toString(command.getPid())));
                        logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Killing given PID.."));
                        if (command.getPid() > 0)
                        {
                            int retstatus = kill(command.getPid(),SIGKILL);
                            if (retstatus == 0) //termination is successfully done
                            {
                                string resp = response.createTerminateMessage(environment.getAgentUuidValue(),
                                        command.getRequestSequenceNumber(),command.getSource(),command.getTaskUuid());
                                connection->sendMessage(resp);
                                logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Terminate success Response:", resp));
                            }
                            else if (retstatus == -1) //termination is failed
                            {
                                string resp = response.createFailTerminateMessage(environment.getAgentUuidValue(),
                                        command.getRequestSequenceNumber(),command.getSource(),command.getTaskUuid());
                                connection->sendMessage(resp);
                                logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>", "Terminate Fail Response! Received PID:", toString(command.getPid())));
                            }
                        }
                        else
                        {
                            logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>", "Irrelevant Terminate Request"));
                        }
                    }
                    else if (command.getType()=="INOTIFY_CREATE_REQUEST")
                    {
                        logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>","executing INOTIFY_REQUEST.."));
                        for (unsigned int i=0; i<command.getWatchArguments().size();i++)
                        {
                            Watcher.addWatcher(command.getWatchArguments()[i]);
                            logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>","adding Watcher: ",
                                        command.getWatchArguments()[i]));
                        }
                        Watcher.stats();
                        sendout = response.createInotifyShowMessage(environment.getAgentUuidValue(),response.getConfPoints());
                        connection->sendMessage(sendout);
                        Watcher.stats();
                        logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>", "Sending Inotify Show Message: ",sendout));
                    }
                    else if (command.getType()=="INOTIFY_REMOVE_REQUEST")
                    {
                        logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>", "executing INOTIFY_CANCEL_REQUEST.."));
                        for (unsigned int i = 0; i < command.getWatchArguments().size(); i++)
                        {
                            Watcher.eraseWatcher(command.getWatchArguments()[i]);
                            logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>", "Erasing Watcher: ",
                                        command.getWatchArguments()[i]));
                        }
                        Watcher.stats();
                        sendout = response.createInotifyShowMessage(environment.getAgentUuidValue(), response.getConfPoints());
                        connection->sendMessage(sendout);
                        Watcher.stats();
                        logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>", "Sending Inotify Show Message: ", sendout));
                    }
                    else if (command.getType()=="INOTIFY_LIST_REQUEST")
                    {
                        logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>", "executing INOTIFY_SHOW_REQUEST.."));
                        Watcher.stats();
                        sendout = response.createInotifyShowMessage(environment.getAgentUuidValue(),response.getConfPoints());
                        connection->sendMessage(sendout);
                        Watcher.stats();
                        logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>", "Sending Inotify Show Message: ",sendout));
                    }
                }
                else
                {
                    logMain.writeLog(3, logMain.setLogData("<SubutaiAgent>","Failed at parsing Json String: ",input));

                    if (input.size() >= 10000)
                    {
                        connection->sendMessage(response.createResponseMessage(environment.getAgentUuidValue(),9999999,
                                    command.getRequestSequenceNumber(),1,
                                    "Command Size is greater than Maximum Size !! ","",command.getSource(),command.getTaskUuid()));
                        connection->sendMessage(response.createExitMessage(environment.getAgentUuidValue(),9999999,
                                    command.getRequestSequenceNumber(),2,
                                    command.getSource(),command.getTaskUuid(),1));
                    }
                    else
                    {
                        connection->sendMessage(response.createResponseMessage(environment.getAgentUuidValue(),9999999,
                                    command.getRequestSequenceNumber(),1,
                                    "Command is not a valid Json string !!","",command.getSource(),command.getTaskUuid()));
                        connection->sendMessage(response.createExitMessage(environment.getAgentUuidValue(),9999999,
                                    command.getRequestSequenceNumber(),2,
                                    command.getSource(),command.getTaskUuid(),1));
                    }
                }
            }
            else
            {
                if (currentProcess < ncores)
                {
                    ifstream file2("/etc/subutai-agent/commandQueue.txt");
                    if (file2.peek() != ifstream::traits_type::eof())
                    {
                        ofstream file3("/etc/subutai-agent/commandQueue2.txt");
                        input = "";
                        getline(file2, str2);
                        input = str2;
                        while(getline(file2, str2))
                        {
                            file3 << str2 << endl;
                        }
                        file3.close();
                        rename("/etc/subutai-agent/commandQueue2.txt", "/etc/subutai-agent/commandQueue.txt");
                        logMain.writeLog(6, logMain.setLogData("<SubutaiAgent>","Message Fetched from internal queue!"));
                        if (input != "\n" && command.deserialize(input))
                        {
                            logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Execute operation is starting.."));
                            SubutaiThread* subprocess = new SubutaiThread;
                            subprocess->getLogger().setLogLevel(logMain.getLogLevel());
                            command.setUuid(environment.getAgentUuidValue()); /*command uuid should be set to agents uuid */
                            SubutaiContainer* target_container = cman.findContainerById(command.getUuid());
                            if (target_container)
                                pidList.push_back(subprocess->threadFunction(&messageQueue, &command, argv, target_container));
                            else
                                pidList.push_back(subprocess->threadFunction(&messageQueue, &command, argv));
                            currentProcess++;
                            delete subprocess;
                        }
                        else
                        {
                            cout << "error!" << endl;
                        }
                    }
                }
            }
        } catch(const std::exception& error) {
            logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>","Exception is raised: ", error.what()));
            cout << error.what() << endl;
        }
    }
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    logMain.writeLog(7, logMain.setLogData("<SubutaiAgent>", "Subutai Agent is closing Successfully.."));
    logMain.closeLogFile();
    kill(getpid(), SIGKILL);
    mosqpp::lib_cleanup();
    return 0;
}