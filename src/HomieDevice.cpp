#include "HomieDevice.h"
#include "HomieNode.h"
void HomieLibDebugPrint(const char * szText);


#if defined(USE_ETHERNET) & defined(ARDUINO_ARCH_ESP32)
#include <ETH.h>
#endif


#define csprintf(...) { char szTemp[256]; snprintf(szTemp,255,__VA_ARGS__); szTemp[255]=0; HomieLibDebugPrint(szTemp); }


static std::vector<HomieDebugPrintCallback> vecDebugPrint;

const int ipub_qos=1;
#if defined(USE_PUBSUBCLIENT)
const int sub_qos=1;
#else
const int sub_qos=2;
#endif

void HomieLibRegisterDebugPrintCallback(HomieDebugPrintCallback cb)
{
	vecDebugPrint.push_back(cb);

}

void HomieLibDebugPrint(const char * szText)
{
	for(size_t i=0;i<vecDebugPrint.size();i++)
	{
		vecDebugPrint[i](szText);
	}

}


HomieDevice * pToken=NULL;

bool AllowInitialPublishing(HomieDevice * pSource)
{
	if(pToken==pSource) return true;
	if(!pToken)
	{
		pToken=pSource;
		return true;
	}
	return false;
}

void FinishInitialPublishing(HomieDevice * pSource)
{
	if(pToken==pSource)
	{
		pToken=NULL;
	}
}


//#define HOMIELIB_VERBOSE

#if defined(USE_PANGOLIN)
void PangoError(uint8_t err1, int err2)
{
	csprintf("PANGO ERROR err1=%i err2=%i\n",err1,err2);
}
#endif


HomieDevice::HomieDevice()
{
#if defined(USE_ARDUINOMQTT)
	pMQTT=new MQTTClient(ARDUINOMQTT_BUFSIZE);
#elif defined(USE_PUBSUBCLIENT)
	pMQTT=new PubSubClient(net);
#endif
}

HomieDevice::~HomieDevice()
{
#if defined(USE_ARDUINOMQTT)
	delete pMQTT;
#elif defined(USE_PUBSUBCLIENT)
	delete pMQTT;
#endif
}

bool HomieDevice::GetEnableMQTT()
{
	if(bEnableMQTT && strMqttServerIP.length()>0)
	{
		return true;
	}

	return false;
}

#ifdef HOMIELIB_CONNECT_ASYNC
void TaskConnect(void * parameter)
{
	((HomieDevice *) parameter)->TaskConnect();
}

void HomieDevice::TaskConnect()
{
	while(1)
	{
		vTaskDelay(10);

		if(bDoConnect)
		{
			csprintf("DoConnect!\n");
			bDoConnect=false;

			int ret=pMQTT->connect(strClientID.c_str(), strMqttUserName.c_str(), strMqttPassword.c_str(), szWillTopic, 1, 1, "lost");
			if(ret)
			{
				csprintf("Success\n");
				onConnect(false);
			}
			else
			{
				bConnecting=false;
				csprintf("Failure!\n");
			}

		}


	}
}

#endif

void HomieDevice::Init()
{
	if(bInitialized) return;

	strTopic=String("homie/")+strID;
	strcpy(szWillTopic,String(strTopic+"/$state").c_str());

	if(!vecNode.size())
	{
		HomieNode * pNode=NewNode();

		pNode->strID="dummy";
		pNode->strFriendlyName="No Nodes";

/*
 	 	lots of mqtt:homie300:srvrm-lightsense:dummy#dummy triggered in the log

  		HomieProperty * pProp=pNode->NewProperty();
		pProp->strID="dummy";
		pProp->strFriendlyName="No Properties";
		pProp->bRetained=false;
		pProp->datatype=homieString;*/

	}

	for(size_t a=0;a<vecNode.size();a++)
	{
		vecNode[a]->Init();
	}


#if defined(USE_PANGOLIN) | defined(USE_ASYNCMQTTCLIENT)

	mqtt.setWill(szWillTopic,1,true,"lost",4);

	mqtt.onConnect(std::bind(&HomieDevice::onConnect, this, std::placeholders::_1));
	mqtt.onDisconnect(std::bind(&HomieDevice::onDisconnect, this, std::placeholders::_1));
	mqtt.onMessage(std::bind(&HomieDevice::onMqttMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));

#elif defined(USE_ARDUINOMQTT)

	pMQTT->setWill(szWillTopic,"lost",true,2);

	pMQTT->onMessageAdvanced([this](MQTTClient *client, char topic[], char bytes[], int len)
			{
				this->onClientCallbackAdvanced(client,topic,bytes,len);
			});

#elif defined(USE_PUBSUBCLIENT)
	pMQTT->setCallback([this](char* topic, byte* payload, unsigned int length)
			{
				onMqttMessage(topic,payload,length);
			});
#endif

#if defined(USE_PANGOLIN)
	mqtt.onError(PangoError);
#endif

	bSendError=false;


	uint8_t mac[6];
	WiFi.macAddress(mac);

	char szMacString[10];
	sprintf(szMacString,"%02x%02x%02x",mac[3],mac[4],mac[5]);

	String strClientID=strID;
	strClientID += "-";
	strClientID += szMacString;

#ifdef HOMIELIB_CONNECT_ASYNC
	if(!bInitialized)
	{
		xTaskCreate(
		  ::TaskConnect, /* Function to implement the task */
		  "TaskHomieConnect", /* Name of the task */
		  10000,  /* Stack size in words */
		  this,  /* Task input parameter */
		  1,  /* Priority of the task */
		  &hTaskConnect /* Task handle. */
		  );
	}
#endif


	bInitialized=true;
}

void HomieDevice::DoDisconnect()
{
/*#if defined(ARDUINO_ARCH_ESP32)
	std::lock_guard<std::mutex> lck(mutexPublish);
#endif*/
#if defined(USE_PANGOLIN) | defined(USE_ASYNCMQTTCLIENT)
	mqtt.disconnect(false);
#elif defined(USE_ARDUINOMQTT) | defined(USE_PUBSUBCLIENT)
	pMQTT->disconnect();
#endif
}

void HomieDevice::Quit()
{
	Publish(String(strTopic+"/$state").c_str(), 1, true, "disconnected");
	DoDisconnect();
	bInitialized=false;
}

bool HomieDevice::IsConnected()
{
#if defined(USE_PANGOLIN) | defined(USE_ASYNCMQTTCLIENT)
	return mqtt.connected();
#elif defined(USE_ARDUINOMQTT) | defined(USE_PUBSUBCLIENT)
	return pMQTT->connected();
#endif
}

bool HomieDevice::IsReady()
{
	return IsConnected() && bInitialPublishingDone;
}


int iWiFiRSSI=0;

void HomieDevice::Loop()
{
	if(!bInitialized) return;


	if((int) (millis()-ulLastLoopTimestamp)>=iMainLoopInterval_ms)
	{
		ulLastLoopTimestamp+=iMainLoopInterval_ms;
	}
	else
	{
		return;
	}


	bool bEvenSecond=false;

	if((int) (millis()-ulLastLoopSecondCounterTimestamp)>=1000)
	{
		ulLastLoopSecondCounterTimestamp+=1000;
		ulSecondCounter_Uptime++;
		if(WiFi.status() == WL_CONNECTED) ulSecondCounter_WiFi++;
#if defined(USE_ETHERNET) & defined(ARDUINO_ARCH_ESP32)
		if(ETH.localIP()!=0) ulSecondCounter_Ethernet++;
#endif

		if(IsConnected()) ulSecondCounter_MQTT++;


		if(bRapidUpdateRSSI && (ulSecondCounter_Uptime & 1))
		{
			int iWiFiRSSI_Current=WiFi.RSSI();
			if(iWiFiRSSI!=iWiFiRSSI_Current)
			{
				iWiFiRSSI=iWiFiRSSI_Current;

				Publish( String(strTopic+"/$stats/signal").c_str(), 2, true, String(iWiFiRSSI).c_str());
			}
		}

		bEvenSecond=true;
	}



	static uint32_t ulFreeHeap=0xFFFFFFF;
#if defined(ARDUINO_ARCH_ESP8266)
	static uint8_t uHeapFrag=0;
	#if !defined(ARDUINO_ESP8266_MAJOR) || ARDUINO_ESP8266_MAJOR < 3
		static uint16_t ulFreeHeapContig=0xFFFF;
	#else
		static uint32_t ulFreeHeapContig=0xFFFFFFF;
	#endif
#endif

	if(bEvenSecond)
	{
		ulFreeHeap=min(ulFreeHeap,ESP.getFreeHeap());
#if defined(ARDUINO_ARCH_ESP8266)
		ulFreeHeapContig=min(ulFreeHeapContig,ESP.getMaxFreeBlockSize());
		uHeapFrag=max(uHeapFrag,ESP.getHeapFragmentation());
#else
		ulFreeHeapContig=min(ulFreeHeapContig,ESP.getMaxAllocHeap());
#endif



	}

	bool bConnected=false;
	if(WiFi.status() == WL_CONNECTED) bConnected=true;

#if defined(USE_ETHERNET) & defined(ARDUINO_ARCH_ESP32)
	if(ETH.localIP()!=0) bConnected=true;
#endif

	if(!bConnected)
	{
		ulSecondCounter_WiFi=0;
		ulSecondCounter_MQTT=0;
#if defined(USE_ETHERNET) & defined(ARDUINO_ARCH_ESP32)
		ulSecondCounter_Ethernet=0;
#endif

		return;
	}

#if defined(USE_ARDUINOMQTT) | defined(USE_PUBSUBCLIENT)
	pMQTT->loop();
#endif

	if(IsConnected())
	{
		if(!GetEnableMQTT())
		{
			if(bWasConnected)
			{
				bWasConnected=false;
				Quit();
			}
			return;
		}

		bWasConnected=true;

		DoInitialPublishing();

//		pubsubClient.loop();

		if((int) (millis()-ulHomieStatsTimestamp)>=30000)
		{
			bool bError=false;

			if(bInitialPublishingDone)
			{
				if(iRePublishReady<2 || (iRePublishReady & 15)==6)		//re-publish once in a while
				{
					bError |= 0==Publish(String(strTopic+"/$state").c_str(), ipub_qos, true, "ready");
				}
				iRePublishReady++;
			}

			String strExtensions="org.homie.legacy-stats:0.1.1:[4.x]";

			if(strFirmwareName.length() || strFirmwareVersion.length())
			{
				strExtensions+=",org.homie.legacy-firmware:0.1.1:[4.x]";
			}

			bError |= 0==Publish(String(strTopic+"/$extensions").c_str(), 2, true, strExtensions.c_str());

			if(strFirmwareName.length())
			{
				bError |= 0==Publish(String(strTopic+"/$fw/name").c_str(), 2, true, strFirmwareName.c_str());
			}

			if(strFirmwareVersion.length())
			{
				bError |= 0==Publish(String(strTopic+"/$fw/version").c_str(), 2, true, strFirmwareVersion.c_str());
			}


			bError |= 0==Publish(String(strTopic+"/$stats/uptime").c_str(), 2, true, String(ulSecondCounter_Uptime).c_str());
			bError |= 0==Publish(String(strTopic+"/$stats/uptime-wifi").c_str(), 2, true, String(ulSecondCounter_WiFi).c_str());
#if defined(USE_ETHERNET) & defined(ARDUINO_ARCH_ESP32)
			bError |= 0==Publish(String(strTopic+"/$stats/uptime-ethernet").c_str(), 2, true, String(ulSecondCounter_Ethernet).c_str());
#endif
			bError |= 0==Publish(String(strTopic+"/$stats/uptime-mqtt").c_str(), 2, true, String(ulSecondCounter_MQTT).c_str());
			bError |= 0==Publish(String(strTopic+"/$stats/signal").c_str(), 2, true, String(WiFi.RSSI()).c_str());
			bError |= 0==Publish(String(strTopic+"/$stats/freeheap").c_str(), 2, true, String(ulFreeHeap).c_str());

			bError |= 0==Publish(String(strTopic+"/$stats/freeheap_contiguous").c_str(), 2, true, String(ulFreeHeapContig).c_str());


			ulFreeHeap=0xFFFFFFF;
#if defined(ARDUINO_ARCH_ESP8266)
			ulFreeHeapContig=0xFFFF;
			bError |= 0==Publish(String(strTopic+"/$stats/heapfrag").c_str(), 2, true, String(uHeapFrag).c_str());
			uHeapFrag=0;
#else
			ulFreeHeapContig=0xFFFFFFF;
#endif


			if(bError)
			{
				ulHomieStatsTimestamp=millis()-(30000-GetErrorRetryFrequency());	//retry in a while
			}
			else
			{
				ulHomieStatsTimestamp=millis();
				bTelemetrySent=true;
			}

//			csprintf("Periodic publishing: %i, %i, %i\n",pub_return[0],pub_return[1],pub_return[2]);
		}

		if(bDoPublishDefaults && (int) (millis()-ulPublishDefaultsTimestamp)>0)
		{
			bDoPublishDefaults=0;

			for(size_t a=0;a<vecNode.size();a++)
			{
				vecNode[a]->PublishDefaults();
			}
		}

		DoLazyPublishing();


	}
	else
	{

		//csprintf("not connected. bConnecting=%i\n",bConnecting);

		ulSecondCounter_MQTT=0;

		ulHomieStatsTimestamp=millis()-1000000;

		bTelemetrySent=false;

		if(GetEnableMQTT())
		{

			if(!bConnecting)
			{

				//csprintf("millis()-ulLastReconnect=%lu  interval=%lu count=%lu\n",millis()-ulLastReconnect,GetReconnectInterval(),ulMqttReconnectCount);

				if(!ulLastReconnect || (millis()-ulLastReconnect)>GetReconnectInterval())
				{
					//csprintf("x");

					IPAddress ip;
					ip.fromString(strMqttServerIP);
					//ip.fromString("172.22.22.99");

					ulLastReconnect=millis();
					ulMqttReconnectCount++;

					csprintf("Connecting to MQTT server %s... (%s)\n",strMqttServerIP.c_str(),GetMqttLibraryID());
					bConnecting=true;
					bSendError=false;
					bInitialPublishingDone=false;

					ulConnectTimestamp=millis();

					
#if defined(USE_PANGOLIN) | defined(USE_ASYNCMQTTCLIENT)
					mqtt.setServer(ip,1883);//1883
					mqtt.setCredentials(strMqttUserName.c_str(), strMqttPassword.c_str());
					mqtt.connect();
#elif defined(USE_ARDUINOMQTT)

					pMQTT->begin(ip, net);
					
					int ret=pMQTT->connect( strClientID.c_str(), strMqttUserName.c_str(), strMqttPassword.c_str());
					if(ret)
					{
						//csprintf("MQTT connect %s %s %s returned %i\n",strID.c_str(), strMqttUserName.c_str(), strMqttPassword.c_str(),ret);
						onConnect(false);
					}
#elif defined(USE_PUBSUBCLIENT)
					pMQTT->setServer(ip,1883);
					csprintf("connecting with ID %s\n",strID.c_str());

#ifdef HOMIELIB_CONNECT_ASYNC
					bDoConnect=true;
#else

					int ret=pMQTT->connect(strClientID.c_str(), strMqttUserName.c_str(), strMqttPassword.c_str(), szWillTopic, 1, 1, "lost");
					if(ret)
					{
						onConnect(false);
					}
#endif
#endif

				}
			}
			else
			{
				//if we're still not connected after a minute, try again
				if(!ulConnectTimestamp || (millis()-ulConnectTimestamp)>60000)
				{
					csprintf("Reconnect needed, dangling flag\n");
					bConnecting=false;
					DoDisconnect();
				}

			}
		}
	}

#if defined(USE_ARDUINOMQTT) | defined(USE_PUBSUBCLIENT)
	for(std::list<HomieProperty *>::iterator iter=listUnsubQueue.begin();iter!=listUnsubQueue.end();iter++)
	{
#if defined(USE_ARDUINOMQTT)
		pMQTT->unsubscribe((*iter)->GetTopic());
#elif defined(USE_PUBSUBCLIENT)
		pMQTT->unsubscribe((*iter)->GetTopic().c_str());
#endif

	}
	listUnsubQueue.clear();
#endif


}

void HomieDevice::onConnect(bool sessionPresent)
{
	if(sessionPresent)	//squelch unused parameter warning
	{
	}
#ifdef HOMIELIB_VERBOSE
	csprintf("onConnect... %p\n",this);
#endif
	bConnecting=false;

	bDoInitialPublishing=true;
	iInitialPublishing=0;
	iInitialPublishing_Node=0;
	iInitialPublishing_Prop=0;
	iPubCount_Props=0;

	ulSecondCounter_MQTT=0;

	for(size_t i=0;i<vecNode.size();i++)
	{
		HomieNode & node=*vecNode[i];
		for(size_t j=0;j<node.vecProperty.size();j++)
		{
			node.vecProperty[j]->SetInitialPublishingDone(false);
			node.vecProperty[j]->SetNeedsPublish(false);
		}
	}

	mapIncoming.clear();

#if defined(USE_ARDUINOMQTT) | defined(USE_PUBSUBCLIENT)
	listUnsubQueue.clear();
#endif

}


#if defined(USE_PANGOLIN)
void HomieDevice::onDisconnect(int8_t reason)
{
	if(reason==TCP_DISCONNECTED)
	{
	}
#elif defined(USE_ASYNCMQTTCLIENT)
void HomieDevice::onDisconnect(AsyncMqttClientDisconnectReason reason)
{
	if(reason==AsyncMqttClientDisconnectReason::TCP_DISCONNECTED)
	{
	}
#elif defined(USE_ARDUINOMQTT) | defined(USE_PUBSUBCLIENT)
void HomieDevice::onDisconnect(int8_t reason)
{
	(void)(reason);
#endif
	csprintf("onDisconnect...");
	if(bConnecting)
	{
		ulLastReconnect=millis();
		bConnecting=false;
		//csprintf("onDisconnect...   reason %i.. lr=%lu\n",reason,ulLastReconnect);
		//csprintf("MQTT server connection failed. Retrying in %lums\n",GetReconnectInterval());
	}
	else
	{
		if(!GetEnableMQTT())
		{
			csprintf("MQTT server disconnected\n");
		}
		else
		{
			csprintf("MQTT server connection lost\n");
		}
	}
}

#if defined(USE_PANGOLIN)
void HomieDevice::onMqttMessage(const char* topic, uint8_t * payload, PANGO_PROPS properties, size_t len, size_t index, size_t total)
{
#elif defined(USE_ASYNCMQTTCLIENT)
void HomieDevice::onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
#elif defined(USE_ARDUINOMQTT)
void HomieDevice::onClientCallbackAdvanced(MQTTClient *client, char topic[], char payload[], int len)
{
	void * properties=NULL;
	uint8_t total=0; uint8_t index=0;
	(void)(client);
#elif defined(USE_PUBSUBCLIENT)
void HomieDevice::onMqttMessage(char* topic, byte* payload, unsigned int len)
{
	void * properties=NULL;
	uint8_t total=0; uint8_t index=0;
#endif
	String strTopic=topic;
	_map_incoming::const_iterator citer=mapIncoming.find(strTopic);

	if(fnMessageCallback && fnMessageCallback(topic,(uint8_t *) payload,len)) return;

	if(citer!=mapIncoming.end())
	{
		HomieProperty * pProp=citer->second;
		if(pProp)
		{

			pProp->OnMqttMessage(topic, payload, properties, len, index, total);

		}
	}


	//csprintf("RECEIVED %s %s\n",topic,payload);
}


HomieProperty * HomieDevice::NewSubscription(const String & strTopic)
{
	if(!vecNode.size())
	{
		HomieNode * pNode=NewNode();
		pNode->strID="subs";
		pNode->strFriendlyName="MQTT subs";
	}

	HomieProperty * ret;


	_map_incoming::const_iterator citer=mapPlainSubscriptions.find(strTopic);
	if(citer!=mapPlainSubscriptions.end())
	{
#ifdef HOMIELIB_VERBOSE
		csprintf("PIGGYBACK %s\n",strTopic.c_str());
#endif
		ret=citer->second;
	}
	else
	{
		ret=vecNode[0]->NewProperty();
		ret->SetStandardMQTT(strTopic);
		mapPlainSubscriptions[strTopic]=ret;
	}



	return ret;
}


HomieNode * HomieDevice::NewNode()
{
	HomieNode * ret=new HomieNode;
	vecNode.push_back(ret);
	ret->pParent=this;

	return ret;
}


void HomieDevice::HandleInitialPublishingError()
{
	csprintf("Initial publishing error at stage %i, retrying in %i\n",iInitialPublishing,GetErrorRetryFrequency());

	ulInitialPublishing=millis()+GetErrorRetryFrequency();
}


void HomieDevice::DoLazyPublishing()
{
	if(ulLazyPublishing!=0 && (int) (millis()-ulLazyPublishing)<iInitialPublishingThrottle_ms)
	{
		return;
	}

	ulLazyPublishing=millis();

	if(!vecNode.size()) return;

	for(int i=0;i<2;i++)
	{
		if(iLazyPublishingNodeIdx>=(int) vecNode.size())
		{
			iLazyPublishingNodeIdx=0;
			iLazyPublishingPropIdx=0;
		}

		if(i==0 && iLazyPublishingPropIdx >= (int) vecNode[iLazyPublishingNodeIdx]->vecProperty.size())
		{
			iLazyPublishingNodeIdx++;

			iLazyPublishingPropIdx=0;
		}
	}

	if(iLazyPublishingPropIdx < (int) vecNode[iLazyPublishingNodeIdx]->vecProperty.size())
	{
		HomieProperty * pProp=vecNode[iLazyPublishingNodeIdx]->vecProperty[iLazyPublishingPropIdx];

		if(pProp->GetNeedsPublish())
		{
			//csprintf("LAZY %i:%i\n",iLazyPublishingNodeIdx,iLazyPublishingPropIdx);
			pProp->Publish();
			pProp->SetNeedsPublish(false);
		}

	}

	iLazyPublishingPropIdx++;

}

void HomieDevice::DoInitialPublishing()
{
#if defined(USE_ARDUINOMQTT)
	MQTTClient & mqtt=*pMQTT;
#elif defined(USE_PUBSUBCLIENT)
	PubSubClient & mqtt=*pMQTT;
#endif


	if(!bDoInitialPublishing)
	{
		iInitialPublishing=0;
		ulInitialPublishing=0;

		return;
	}

	if(ulInitialPublishing!=0 && (int) (millis()-ulInitialPublishing)<iInitialPublishingThrottle_ms)
	{
		return;
	}

	if(!ulInitialPublishing)
	{
		csprintf("%s MQTT Initial Publishing...\n",strTopic.c_str());
		iPubCount_Props=0;
	}

	ulInitialPublishing=millis();


	if(!AllowInitialPublishing(this)) return;


#ifdef HOMIELIB_VERBOSE
	if(bDebug) csprintf("IPUB: %i        Node=%i  Prop=%i\n",iInitialPublishing, iInitialPublishing_Node, iInitialPublishing_Prop);
#endif


	if(iInitialPublishing==0)
	{
		bool bError=false;
		bError |= 0==Publish(String(strTopic+"/$state").c_str(), ipub_qos, true, "init");
		bError |= 0==Publish(String(strTopic+"/$homie").c_str(), ipub_qos, true, "4.0.0");
		bError |= 0==Publish(String(strTopic+"/$name").c_str(), ipub_qos, true, strFriendlyName.c_str());
		if(bError)
		{
			HandleInitialPublishingError();
		}
		else
		{
			iInitialPublishing=1;
		}

		return;
	}

	if(iInitialPublishing==1)
	{
		bool bError=false;
		String strIP=WiFi.localIP().toString();
		String strMAC=WiFi.macAddress();
#if defined(USE_ETHERNET) & defined(ARDUINO_ARCH_ESP32)
		if(ETH.localIP()!=0)
		{
			strIP=ETH.localIP().toString();
			strMAC=ETH.macAddress();
		}
#endif

		bError |= 0==Publish(String(strTopic+"/$localip").c_str(), ipub_qos, true, strIP.c_str());
		bError |= 0==Publish(String(strTopic+"/$mac").c_str(), ipub_qos, true, strMAC.c_str());
		bError |= 0==Publish(String(strTopic+"/$extensions").c_str(), ipub_qos, true, "");

		if(bError)
		{
			HandleInitialPublishingError();
		}
		else
		{
			iInitialPublishing=2;
		}
		return;
	}

	if(iInitialPublishing==2)
	{
		bool bError=false;

		bError |= 0==Publish(String(strTopic+"/$stats").c_str(), ipub_qos, true, "uptime,signal,uptime-wifi,"
#if defined(USE_ETHERNET) & defined(ARDUINO_ARCH_ESP32)
				"uptime-ethernet,"
#endif
				"uptime-mqtt,freeheap,freeheap_contiguous,heapfrag");
		bError |= 0==Publish(String(strTopic+"/$stats/interval").c_str(), ipub_qos, true, "60");

		String strNodes;
		for(size_t i=0;i<vecNode.size();i++)
		{
			strNodes+=vecNode[i]->strID;
			if(i<vecNode.size()-1) strNodes+=",";
		}

#ifdef HOMIELIB_VERBOSE
		if(bDebug) csprintf("NODES: %s\n",strNodes.c_str());
#endif

		bError |= 0==Publish(String(strTopic+"/$nodes").c_str(), ipub_qos, true, strNodes.c_str());

		if(bError)
		{
			HandleInitialPublishingError();
		}
		else
		{
			iInitialPublishing_Node=0;
			iInitialPublishing=3;
		}
		return;
	}


	if(iInitialPublishing==3)
	{
		bool bError=false;
		int i=iInitialPublishing_Node;
		if(i<(int) vecNode.size())
		{
			HomieNode & node=*vecNode[i];
#ifdef HOMIELIB_VERBOSE
			if(bDebug) csprintf("NODE %i: %s\n",i,node.strFriendlyName.c_str());
#endif

			bError |= 0==Publish(String(node.GetTopic()+"/$name").c_str(), ipub_qos, true, node.strFriendlyName.c_str());
			bError |= 0==Publish(String(node.GetTopic()+"/$type").c_str(), ipub_qos, true, node.strType.c_str());

			String strProperties;
			for(size_t j=0;j<node.vecProperty.size();j++)
			{
				if(!node.vecProperty[j]->GetIsStandardMQTT())
				{
					if(strProperties.length()) strProperties+=",";
					strProperties+=node.vecProperty[j]->strID;
				}
			}

#ifdef HOMIELIB_VERBOSE
			if(bDebug) csprintf("NODE %i: %s has properties %s\n",i,node.strFriendlyName.c_str(),strProperties.c_str());
#endif

			bError |= 0==Publish(String(node.GetTopic()+"/$properties").c_str(), ipub_qos, true, strProperties.c_str());

			if(bError)
			{
				HandleInitialPublishingError();
			}
			else
			{
				iInitialPublishing_Node++;
			}

			return;
		}

		if(i>=(int) vecNode.size())
		{
			iInitialPublishing=4;
			iInitialPublishing_Node=0;
			iInitialPublishing_Prop=0;
		}
	}

	if(iInitialPublishing==4)
	{
		bool bError=false;
		int i=iInitialPublishing_Node;

		if(i<(int) vecNode.size())
		{
			HomieNode & node=*vecNode[i];
#ifdef HOMIELIB_VERBOSE
			if(bDebug) csprintf("NODE %i: %s\n",i,node.strFriendlyName.c_str());
#endif

			int j=iInitialPublishing_Prop;
			if(j<(int) node.vecProperty.size())
			{
				bool bSuccess=false;
				HomieProperty & prop=*node.vecProperty[j];

#ifdef HOMIELIB_VERBOSE
				if(bDebug) csprintf("NODE %i: %s property %s\n",i,node.strFriendlyName.c_str(),prop.strFriendlyName.c_str());
#endif

				if(prop.GetIsStandardMQTT())
				{

					bError |= 0==(bSuccess=mqtt.subscribe(prop.GetTopic().c_str(), sub_qos));
					mapIncoming[prop.GetTopic()]=&prop;
#ifdef HOMIELIB_VERBOSE
					csprintf("SUBSCRIBING to MQTT topic %s (ID=%s): ",prop.GetTopic().c_str(),prop.strID.c_str());
#endif

#ifdef HOMIELIB_VERBOSE
					csprintf("%s\n",bSuccess?"OK":"FAIL");
#endif
				}
				else
				{

					bError |= 0==Publish(String(prop.GetTopic()+"/$name").c_str(), ipub_qos, true, prop.strFriendlyName.c_str());
					bError |= 0==Publish(String(prop.GetTopic()+"/$settable").c_str(), ipub_qos, true, prop.GetSettable()?"true":"false");
					bError |= 0==Publish(String(prop.GetTopic()+"/$retained").c_str(), ipub_qos, true, (prop.GetRetained() || prop.GetFakeRetained())?"true":"false");
					bError |= 0==Publish(String(prop.GetTopic()+"/$datatype").c_str(), ipub_qos, true, GetHomieDataTypeText((eHomieDataType) prop.datatype));
					if(prop.pstrUnit && prop.pstrUnit->length())
					{
						bError |= 0==Publish(String(prop.GetTopic()+"/$unit").c_str(), ipub_qos, true, prop.pstrUnit->c_str());
					}
					if(prop.strFormat.length())
					{
						bError |= 0==Publish(String(prop.GetTopic()+"/$format").c_str(), ipub_qos, true, prop.strFormat.c_str());
					}

					if(prop.GetSettable())
					{
						mapIncoming[prop.GetTopic()]=&prop;
						mapIncoming[prop.GetSetTopic()]=&prop;
						if(prop.GetRetained())
						{
	#ifdef HOMIELIB_VERBOSE
							csprintf("SUBSCRIBING to %s: ",prop.GetTopic().c_str());
	#endif
							if(prop.GetReceivedRetained())
							{
								bError |= 0==(bSuccess=prop.Publish());
							}
							else
							{
								bError |= 0==(bSuccess=mqtt.subscribe(prop.GetTopic().c_str(), sub_qos));
							}
#ifdef HOMIELIB_VERBOSE
							csprintf("%s\n",bSuccess?"OK":"FAIL");
#endif
						}
						else
						{
							bError |= 0==(bSuccess=prop.Publish());
						}
	#ifdef HOMIELIB_VERBOSE
						csprintf("SUBSCRIBING to %s: ",prop.GetSetTopic().c_str());
	#endif
						bError |= 0==(bSuccess=mqtt.subscribe(prop.GetSetTopic().c_str(), sub_qos));
#ifdef HOMIELIB_VERBOSE
						csprintf("%s\n",bSuccess?"OK":"FAIL");
#endif
					}
					else
					{
						if(prop.GetRetained())
						{
							bError |= false==prop.Publish();
						}
					}

					prop.SetInitialPublishingDone(true);



				}




				if(bError)
				{
					HandleInitialPublishingError();
				}
				else
				{
					iInitialPublishing_Prop++;
					iPubCount_Props++;
				}

				return;
			}

			if(j>=(int) node.vecProperty.size())
			{
				iInitialPublishing_Prop=0;
				iInitialPublishing_Node++;
			}
		}

		if(iInitialPublishing_Node>=(int) vecNode.size())
		{
			iInitialPublishing_Node=0;
			iInitialPublishing_Prop=0;
			iInitialPublishing=5;
		}

	}

	if(iInitialPublishing==5)
	{
		bool bError=false;
		bError |= 0==Publish(String(strTopic+"/$state").c_str(), ipub_qos, true, "ready");

		if(bError)
		{
			HandleInitialPublishingError();
		}
		else
		{
			bDoInitialPublishing=false;
			csprintf("Initial publishing complete. %i nodes, %i properties\n",vecNode.size(),iPubCount_Props);
			FinishInitialPublishing(this);

			bInitialPublishingDone=true;

			iRePublishReady=0;

			ulPublishDefaultsTimestamp=millis()+15000;
			bDoPublishDefaults=true;

			ulMqttReconnectCount=0;

		}


	}

}

uint16_t HomieDevice::PublishDirect(const String & topic, uint8_t qos, bool retain, const String & payload)
{
	return PublishDirectUint8(topic.c_str(),qos,retain,(const uint8_t *) payload.c_str(),payload.length());

}

uint16_t HomieDevice::PublishDirectUint8(const char * topic, uint8_t qos, bool retain, const uint8_t * payload, uint32_t length)
{

/*#if defined(ARDUINO_ARCH_ESP32)
	std::lock_guard<std::mutex> lck(mutexPublish);
#endif*/

	//csprintf("topic: %s, payload: %s, len=%u\n",topic,payload,length);

#if defined(USE_PANGOLIN)
	mqtt.publish(topic, qos, retain, (uint8_t *) payload, length, 0);
	return 1;
#elif defined(USE_ASYNCMQTTCLIENT)
	return mqtt.publish(topic, qos, retain, (const char *) payload, length);
#elif defined(USE_ARDUINOMQTT)
	return pMQTT->publish(topic, payload, retain, qos)==true;
#elif defined(USE_PUBSUBCLIENT)
	(void)(qos);
	return pMQTT->publish(topic, payload, length, retain);
#endif

}

bool bFailPublish=false;

uint16_t HomieDevice::Publish(const char* topic, uint8_t qos, bool retain, const char* payload, size_t length, bool dup, uint16_t message_id)
{

	(void)(dup);
	(void)(message_id);
	if(!IsConnected()) return 0;
	uint16_t ret=0;

	if(!bFailPublish)
	{
		if(!length) length=strlen(payload);
		//csprintf("publishing topic %s data %s (length %i)\n",payload,length);

		ret=PublishDirectUint8(topic,qos,retain,(const uint8_t *) payload,length);
/*
#if defined(USE_PANGOLIN)
		mqtt.publish(topic, qos, retain, (uint8_t *) payload, length, false);
#elif defined(USE_ASYNCMQTTCLIENT)
		ret=mqtt.publish(topic,qos,retain,payload,length,dup,message_id);
#elif defined(USE_ARDUINOMQTT)
		ret=pMQTT->publish(topic, payload, retain, qos);
		//csprintf("publish B %s=%s success=%i\n",topic,payload,ret);
#elif defined(USE_PUBSUBCLIENT)
		(void)(qos);
		ret=pMQTT->publish(topic, (uint8_t *) payload, length, retain);
#endif
*/

		if(!ret)
		{
			csprintf("ERROR publishing %s  %s\n",topic,payload);
		}

		//PublishPacket pub(topic,qos,retain,payload,length(),false,0);
		//ret=mqtt.publish(topic,qos,retain,payload,length,dup,message_id);
		//ret=true;
	}

	//csprintf("Publish %s: ret %i\n",topic,ret);

	if(!ret)
	{	//failure
		if(!bSendError)
		{
			bSendError=true;
			ulSendErrorTimestamp=millis();
		}
		else
		{
			if((int) (millis()-ulSendErrorTimestamp) > 60000)	//a full minute with no successes
			{
				csprintf("Full minute with no publish successes, disconnect and try again\n");
				DoDisconnect();
				bSendError=false;
				bConnecting=false;
			}
		}
	}
	else
	{	//success
		bSendError=false;

	}

	yield();
	return ret;
}


String HomieDeviceName(const char * in)
{
	String ret;

	enum eLast
	{
		last_hyphen,
		last_upper,
		last_lower,
		last_number,
	};

	eLast last=last_hyphen;

	while(*in)
	{
		char input=*in++;

		if(input>='0' && input<='9')
		{
			ret+=input;
			last=last_number;
		}
		else if(input>='A' && input<='Z')
		{
			if(last==last_lower)
			{
				ret+='-';
			}

			ret+=(char) (input | 0x20);

			last=last_upper;
		}
		else if(input>='a' && input<='z')
		{
			ret+=input;
			last=last_lower;
		}
		else
		{
			if(last!=last_hyphen)
			{
				ret+="-";
			}

			last=last_hyphen;
		}

	}

	return ret;
}

bool HomieParseRGB(const char * in, uint32_t & rgb)
{
	int r, g, b;

	if (sscanf(in, "%d,%d,%d", &r, &g, &b) == 3)
	{
		rgb=(r<<16) + (g<<8) + (b<<0);
		return true;
	}
	return false;
}


void HSVtoRGB(float fHueIn, float fSatIn, float fBriteIn, float & fRedOut, float & fGreenOut, float & fBlueOut)
{
	float fHue = (float) fmod(fHueIn / 6.0f, 60);
	float fC = fBriteIn * fSatIn;
	float fL = fBriteIn - fC;
	float fX = (float) (fC * (1.0f - fabs(fmod(fHue * 0.1f, 2.0f) - 1.0f)));

	if(fHue >= 0.0f && fHue < 10.0f) { fRedOut = fC; fGreenOut = fX; fBlueOut = 0; }
	else if(fHue >= 10.0f && fHue < 20.0f) { fRedOut = fX; fGreenOut = fC; fBlueOut = 0; }
	else if(fHue >= 20.0f && fHue < 30.0f) { fRedOut = 0; fGreenOut = fC; fBlueOut = fX; }
	else if(fHue >= 30.0f && fHue < 40.0f) { fRedOut = 0; fGreenOut = fX; fBlueOut = fC; }
	else if(fHue >= 40.0f && fHue < 50.0f) { fRedOut = fX; fGreenOut = 0; fBlueOut = fC; }
	else if(fHue >= 50.0f && fHue < 60.0f) { fRedOut = fC; fGreenOut = 0; fBlueOut = fX; }
	else { fRedOut = 0; fGreenOut = 0; fBlueOut = 0; }

	fRedOut += fL;
	fGreenOut += fL;
	fBlueOut += fL;
}


bool HomieParseHSV(const char * in, uint32_t & rgb)
{
	float h, s, v;

	if (sscanf(in, "%f,%f,%f", &h, &s, &v) == 3)
	{
		float fR, fG, fB;

		HSVtoRGB(h, s*0.01f, v*0.01f,fR, fG, fB);

		int r=fR*256.0f;
		int g=fG*256.0f;
		int b=fB*256.0f;

		if(r>255) r=255;
		if(g>255) g=255;
		if(b>255) b=255;

		rgb=(r<<16) + (g<<8) + (b<<0);
		return true;
	}

	return false;
}


int HomieDevice::GetErrorRetryFrequency()
{
	int iErrorDuration=(int) (millis()-ulSendErrorTimestamp);
	if(iErrorDuration >= 20000)
	{
		return 10000;
	}
	if(iErrorDuration >= 5000)
	{
		return 5000;
	}

	return 1000;
}

uint32_t HomieDevice::GetUptimeSeconds_WiFi()
{
	return ulSecondCounter_WiFi;
}

uint32_t HomieDevice::GetUptimeSeconds_MQTT()
{
	return ulSecondCounter_MQTT;
}

#if defined(USE_ETHERNET) & defined(ARDUINO_ARCH_ESP32)
uint32_t HomieDevice::GetUptimeSeconds_Ethernet()
{
	return ulSecondCounter_Ethernet;
}
#endif

unsigned long HomieDevice::GetReconnectInterval()
{
	unsigned long interval=5000;
	if(ulMqttReconnectCount>=20) interval=60000;
	else if(ulMqttReconnectCount>=15) interval=30000;
	else if(ulMqttReconnectCount>=10) interval=20000;
	else if(ulMqttReconnectCount>=5) interval=10000;
	return interval;
}

void HomieDevice::InitialUnsubscribe(HomieProperty * pProp)
{

#if defined(USE_PANGOLIN) | defined(USE_ASYNCMQTTCLIENT)
	mqtt.unsubscribe(pProp->GetTopic().c_str());
#elif defined(USE_ARDUINOMQTT) | defined(USE_PUBSUBCLIENT)
	listUnsubQueue.push_back(pProp);
#endif

}

