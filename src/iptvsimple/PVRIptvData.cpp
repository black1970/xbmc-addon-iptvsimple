/*
 *      Copyright (C) 2011 Pulse-Eight
 *      http://www.pulse-eight.com/
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include <sstream>
#include <string>
#include <fstream>
#include <map>
#include "tinyxml/XMLUtils.h"
#include "PVRIptvData.h"
#include <zlib.h>

#define M3U_FILE_NAME         "iptv.m3u"
#define M3U_START_MARKER      "#EXTM3U"
#define M3U_INFO_MARKER       "#EXTINF"

#define TVG_FILE_NAME         "xmltv.xml.gz"
#define TVG_INFO_ID_MARKER    "tvg-id=\""
#define TVG_INFO_NAME_MARKER  "tvg-name=\""
#define TVG_INFO_LOGO_MARKER  "tvg-logo=\""

#define GROUP_NAME_MARKER     "group-title=\""

using namespace std;
using namespace ADDON;

PVRIptvData::PVRIptvData(void)
{
  m_iEpgStart = -1;
  m_strDefaultIcon =  "http://www.royalty-free.tv/news/wp-content/uploads/2011/06/cc-logo1.jpg";

  m_strXMLTVUrl = g_strTvgPath;
  m_strM3uUrl = g_strM3UPath;

  m_bEGPLoaded = false;

  if (LoadPlayList() && m_channels.size() > 0)
  {
	  XBMC->QueueNotification(QUEUE_INFO, "Channels loaded.");
  }
}

void *PVRIptvData::Process(void)
{
	m_bEGPLoaded = false;

	XBMC->QueueNotification(QUEUE_INFO, "Start EPG loading.");

	if (LoadEPG())
	{
		XBMC->QueueNotification(QUEUE_INFO, "EPG data is loaded.");
	}

	m_bEGPLoaded = true;

	return NULL;
}

PVRIptvData::~PVRIptvData(void)
{
  m_channels.clear();
  m_groups.clear();
  m_egpChannels.clear();
}

bool PVRIptvData::LoadEPG(void) 
{
	//CStdString strXML = GetFileContents(m_strXMLTVUrl);
	CStdString strXML = GetCachedFileContents(TVG_FILE_NAME, m_strXMLTVUrl);

	if (strXML.IsEmpty()) 
	{
		XBMC->Log(LOG_ERROR, "invalid EPG data (no/invalid data found at '%s')", m_strXMLTVUrl.c_str());
		return false;
	}

	if (strXML.Left(3) == "\x1F\x8B\x08") {
		std::string data;
		if (!gzipInflate(strXML, data))
		{
			XBMC->Log(LOG_ERROR, "invalid EPG data (no/invalid data found at '%s')", m_strXMLTVUrl.c_str());
			return false;
		}
		strXML = data;
	}

    TiXmlDocument xmlDoc;
	if (!xmlDoc.Parse(strXML))
	{
		XBMC->Log(LOG_ERROR, "invalid EPG data (no/invalid data found at '%s')", m_strXMLTVUrl.c_str());
		return false;
	}

    TiXmlElement *pRootElement = xmlDoc.RootElement();
    if (strcmp(pRootElement->Value(), "tv") != 0)
	{
		XBMC->Log(LOG_ERROR, "invalid EPG data (no <tv> tag found)");
		return false;
	}

	std::map<CStdString, int> dic;
	int iBroadCastId = 0;

    TiXmlNode *pChannelNode = NULL;
    while ((pChannelNode = pRootElement->IterateChildren("channel", pChannelNode)) != NULL)
	{
		TiXmlElement *pElement = pChannelNode->ToElement();
		int iChannelId = -1;
		CStdString strTmp;
		CStdString strName;
	
		pElement->Attribute("id", &iChannelId);
		if (iChannelId <= 0) {
			continue;
		}
		if (!XMLUtils::GetString(pChannelNode, "display-name", strName))
		{
			continue;
		}

		PVRIptvEpgChannel egpChannel;
		memset(&egpChannel, 0, sizeof(PVRIptvEpgChannel));

		egpChannel.iId = iChannelId;
		egpChannel.strName = strName;

		m_egpChannels.push_back(egpChannel);
	}

	if (m_egpChannels.size() == 0) 
	{
		return false;
	}

	PVRIptvEpgChannel *currentChannel = NULL;
    while ((pChannelNode = pRootElement->IterateChildren("programme", pChannelNode)) != NULL)
	{
		TiXmlElement *pElement = pChannelNode->ToElement();
		int iChannelId = -1;
		pElement->Attribute("channel", &iChannelId);

		if (iChannelId <= 0)
			continue;

		if (currentChannel == NULL || currentChannel->iId != iChannelId) {
			if ((currentChannel = FindEgpChannelById(iChannelId)) == NULL) {
				continue;
			}
		}

		CStdString strTitle;
		CStdString strCategory;
		CStdString strDesc;
		int iTmpStart;
		int iTmpEnd;

		iTmpStart = ParseDateTime(pElement->Attribute("start"));
		iTmpEnd = ParseDateTime(pElement->Attribute("stop"));

		XMLUtils::GetString(pChannelNode, "title", strTitle);
		XMLUtils::GetString(pChannelNode, "category", strCategory);
		XMLUtils::GetString(pChannelNode, "desc", strDesc);

		PVRIptvEpgEntry entry;
		memset(&entry, 0, sizeof(PVRIptvEpgEntry));

		entry.iBroadcastId = ++iBroadCastId;
		entry.iGenreType = 0;
		entry.iGenreSubType = 0;

		entry.strTitle = strTitle;
		entry.strPlot = strDesc;
		entry.strPlotOutline = "";
		entry.startTime = iTmpStart;
		entry.endTime = iTmpEnd;

		currentChannel->epg.push_back(entry);
	}

	m_bEGPLoaded = true;

	return true;
}

bool PVRIptvData::LoadPlayList(void) 
{
	//CStdString strPlaylistContent = GetFileContents(m_strM3uUrl);
	CStdString strPlaylistContent = GetCachedFileContents(M3U_FILE_NAME, m_strM3uUrl);

	if (strPlaylistContent.IsEmpty())
	{
		XBMC->Log(LOG_ERROR, "invalid palylist data (no/invalid data found at '%s')", m_strM3uUrl.c_str());
		return false;
	}

	char szLine[4096];
	CStdString strLine;
	CStdString strInfo = "";

	std::stringstream stream(strPlaylistContent);

	/* load channels */
	int iUniqueChannelId = 0;
	int iUniqueGroupId = 0;
	int iChannelNum = 0;
	bool isfirst = true;

	PVRIptvChannel tmpChannel;
	memset(&tmpChannel, 0, sizeof(PVRIptvChannel));
	tmpChannel.iTvgId = -1;

	while(stream.getline(szLine, 1024)) 
	{
		strLine = szLine;
		strLine.TrimRight(" \t\r\n");
		strLine.TrimLeft(" \t");

		if (isfirst) 
		{
			isfirst = false;

			if (strLine.Left(3) == "\xEF\xBB\xBF")
			{
				strLine.Delete(0, 3);
			}

			if (strLine.Left((int)strlen(M3U_START_MARKER)) == M3U_START_MARKER) 
			{
				continue;
			}
			else
			{
				// bad file
				return false;
			}
		}

		CStdString	strChnlName;
		CStdString	strTvgId;
		CStdString	strTvgName;
		CStdString	strTvgLogo;
		CStdString	strGroupName;

		if (strLine.Left((int)strlen(M3U_INFO_MARKER)) == M3U_INFO_MARKER) 
		{
			// parse line
			int iColon = (int)strLine.find(":");
			int iComma = (int)strLine.find(",");
			if (iColon >= 0 && iComma >= 0 && iComma > iColon) 
			{
				// parse name
				iComma++;
				strChnlName = strLine.Right((int)strLine.size() - iComma);
				if (strChnlName.IsEmpty()) {
					strChnlName.Format("IPTV Channel %d", iChannelNum + 1);
				}
				tmpChannel.strChannelName = XBMC->UnknownToUTF8(strChnlName);

				// parse info
				iColon++;
				CStdString strInfoLine = strLine.Mid(iColon, iComma - iColon);

				int iTvgIdPos = (int)strInfoLine.find(TVG_INFO_ID_MARKER);
				int iTvgNamePos = (int)strInfoLine.find(TVG_INFO_NAME_MARKER);
				int iTvgLogoPos = (int)strInfoLine.find(TVG_INFO_LOGO_MARKER);
				int iGroupNamePos = (int)strInfoLine.find(GROUP_NAME_MARKER);

				if (iTvgIdPos >= 0) 
				{
					iTvgIdPos += sizeof(TVG_INFO_ID_MARKER) - 1;
					int iTvgIdEndPos = (int)strInfoLine.Find('"', iTvgIdPos);
				  
					if (iTvgIdEndPos >= 0)
					{
						tmpChannel.iTvgId = atoi(strInfoLine.substr(iTvgIdPos, iTvgIdEndPos - iTvgIdPos).c_str());
					}
				}
				else
				{
					tmpChannel.iTvgId = atoi(strInfoLine.c_str());
				}

				if (iTvgNamePos >= 0) 
				{
					iTvgNamePos += sizeof(TVG_INFO_NAME_MARKER) - 1;
					int iTvgNameEndPos = (int)strInfoLine.Find('"', iTvgNamePos);
				  
					if (iTvgNameEndPos >= 0)
					{
						strTvgName = strInfoLine.substr(iTvgNamePos, iTvgNameEndPos - iTvgNamePos);
						tmpChannel.strTvgName = XBMC->UnknownToUTF8(strTvgName);
					}
				}

				if (iTvgLogoPos >= 0) 
				{
					iTvgLogoPos += sizeof(TVG_INFO_LOGO_MARKER) - 1;
					int iTvgLogoEndPos = (int)strInfoLine.Find('"', iTvgLogoPos);
				  
					if (iTvgLogoEndPos >= 0)
					{
						strTvgLogo = strInfoLine.substr(iTvgLogoPos, iTvgLogoEndPos - iTvgLogoPos);
						strTvgLogo = XBMC->UnknownToUTF8(strTvgLogo);

						if (!strTvgLogo.IsEmpty()) {
							strTvgLogo = GetClientFilePath("icons/" + strTvgLogo);
							strTvgLogo.append(".png");
							tmpChannel.strIconPath = strTvgLogo;
						}
					}
				}
				else
				{
					strTvgLogo = XBMC->UnknownToUTF8(strChnlName);
					strTvgLogo = GetClientFilePath("icons/" + strTvgLogo);
					strTvgLogo.append(".png");
					tmpChannel.strIconPath = strTvgLogo;
				}

				if (iGroupNamePos >= 0) 
				{
					iGroupNamePos += sizeof(GROUP_NAME_MARKER) - 1;
					int iGroupNameEndPos = (int)strInfoLine.Find('"', iGroupNamePos);
				  
					if (iGroupNameEndPos >= 0)
					{
						strGroupName = strInfoLine.substr(iGroupNamePos, iGroupNameEndPos - iGroupNamePos);

						PVRIptvChannelGroup group;
						group.strGroupName = XBMC->UnknownToUTF8(strGroupName);
						group.iGroupId = ++iUniqueGroupId;
						group.bRadio = false;

						m_groups.push_back(group);
					}
				}
			}
		} 
		else 
		{
			strChnlName = tmpChannel.strChannelName;
			if (strChnlName.IsEmpty())
			{
				strChnlName = strLine;
			}

			if (!strChnlName.IsEmpty() && !strLine.IsEmpty())
			{
				PVRIptvChannel channel;
				memset(&channel, 0, sizeof(PVRIptvChannel));

				channel.iUniqueId		= ++iUniqueChannelId;
				channel.iChannelNumber	= ++iChannelNum;
				channel.iTvgId			= tmpChannel.iTvgId;
				channel.strChannelName	= tmpChannel.strChannelName;
				channel.strTvgName		= tmpChannel.strTvgName;
				channel.strIconPath		= tmpChannel.strIconPath;
				channel.strStreamURL	= strLine;

				channel.iEncryptionSystem = 0;
				channel.bRadio = false;

				m_channels.push_back(channel);

				if (iUniqueGroupId > 0) 
				{
					m_groups.at(iUniqueGroupId - 1).members.push_back(channel.iChannelNumber);
				}
			}

			tmpChannel.strChannelName = "";
		}
	}
  
	stream.clear();

	if (m_channels.size() == 0)
	{
		XBMC->Log(LOG_ERROR, "invalid palylist data (no/invalid channels found at '%s')", m_strM3uUrl.c_str());
	}

	return true;
}

int PVRIptvData::GetChannelsAmount(void)
{
  return m_channels.size();
}

PVR_ERROR PVRIptvData::GetChannels(ADDON_HANDLE handle, bool bRadio)
{
  for (unsigned int iChannelPtr = 0; iChannelPtr < m_channels.size(); iChannelPtr++)
  {
    PVRIptvChannel &channel = m_channels.at(iChannelPtr);
    if (channel.bRadio == bRadio)
    {
      PVR_CHANNEL xbmcChannel;
      memset(&xbmcChannel, 0, sizeof(PVR_CHANNEL));

      xbmcChannel.iUniqueId         = channel.iUniqueId;
      xbmcChannel.bIsRadio          = channel.bRadio;
      xbmcChannel.iChannelNumber    = channel.iChannelNumber;
      strncpy(xbmcChannel.strChannelName, channel.strChannelName.c_str(), sizeof(xbmcChannel.strChannelName) - 1);
      strncpy(xbmcChannel.strStreamURL, channel.strStreamURL.c_str(), sizeof(xbmcChannel.strStreamURL) - 1);
      xbmcChannel.iEncryptionSystem = channel.iEncryptionSystem;
      strncpy(xbmcChannel.strIconPath, channel.strIconPath.c_str(), sizeof(xbmcChannel.strIconPath) - 1);
      xbmcChannel.bIsHidden         = false;

      PVR->TransferChannelEntry(handle, &xbmcChannel);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

bool PVRIptvData::GetChannel(const PVR_CHANNEL &channel, PVRIptvChannel &myChannel)
{
  for (unsigned int iChannelPtr = 0; iChannelPtr < m_channels.size(); iChannelPtr++)
  {
    PVRIptvChannel &thisChannel = m_channels.at(iChannelPtr);
    if (thisChannel.iUniqueId == (int) channel.iUniqueId)
    {
      myChannel.iUniqueId         = thisChannel.iUniqueId;
      myChannel.bRadio            = thisChannel.bRadio;
      myChannel.iChannelNumber    = thisChannel.iChannelNumber;
      myChannel.iEncryptionSystem = thisChannel.iEncryptionSystem;
      myChannel.strChannelName    = thisChannel.strChannelName;
      myChannel.strIconPath       = thisChannel.strIconPath;
      myChannel.strStreamURL      = thisChannel.strStreamURL;

      return true;
    }
  }

  return false;
}

int PVRIptvData::GetChannelGroupsAmount(void)
{
  return m_groups.size();
}

PVR_ERROR PVRIptvData::GetChannelGroups(ADDON_HANDLE handle, bool bRadio)
{
  for (unsigned int iGroupPtr = 0; iGroupPtr < m_groups.size(); iGroupPtr++)
  {
    PVRIptvChannelGroup &group = m_groups.at(iGroupPtr);
    if (group.bRadio == bRadio)
    {
      PVR_CHANNEL_GROUP xbmcGroup;
      memset(&xbmcGroup, 0, sizeof(PVR_CHANNEL_GROUP));

      xbmcGroup.bIsRadio = bRadio;
      strncpy(xbmcGroup.strGroupName, group.strGroupName.c_str(), sizeof(xbmcGroup.strGroupName) - 1);

      PVR->TransferChannelGroup(handle, &xbmcGroup);
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRIptvData::GetChannelGroupMembers(ADDON_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
  for (unsigned int iGroupPtr = 0; iGroupPtr < m_groups.size(); iGroupPtr++)
  {
    PVRIptvChannelGroup &myGroup = m_groups.at(iGroupPtr);
    if (!strcmp(myGroup.strGroupName.c_str(),group.strGroupName))
    {
      for (unsigned int iChannelPtr = 0; iChannelPtr < myGroup.members.size(); iChannelPtr++)
      {
        int iId = myGroup.members.at(iChannelPtr) - 1;
        if (iId < 0 || iId > (int)m_channels.size() - 1)
          continue;
        PVRIptvChannel &channel = m_channels.at(iId);
        PVR_CHANNEL_GROUP_MEMBER xbmcGroupMember;
        memset(&xbmcGroupMember, 0, sizeof(PVR_CHANNEL_GROUP_MEMBER));

        strncpy(xbmcGroupMember.strGroupName, group.strGroupName, sizeof(xbmcGroupMember.strGroupName) - 1);
        xbmcGroupMember.iChannelUniqueId = channel.iUniqueId;
        xbmcGroupMember.iChannelNumber   = channel.iChannelNumber;

        PVR->TransferChannelGroupMember(handle, &xbmcGroupMember);
      }
    }
  }

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR PVRIptvData::GetEPGForChannel(ADDON_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
	for (unsigned int iChannelPtr = 0, iChannelMax = m_channels.size(); iChannelPtr < iChannelMax; iChannelPtr++)
	{
		PVRIptvChannel &myChannel = m_channels.at(iChannelPtr);
		if (myChannel.iUniqueId != (int) channel.iUniqueId)
			continue;

		if (!m_bEGPLoaded) {
			LoadEPG();
		}

		PVRIptvEpgChannel *egpChannel = NULL;
		if ((egpChannel = FindEgpChannelByPvrChannel(myChannel)) == NULL) {
			return PVR_ERROR_NO_ERROR;
		}

		if (egpChannel->epg.size() == 0) {
			return PVR_ERROR_NO_ERROR;
		}

		for (unsigned int iEntryPtr = 0, iEntrymax = egpChannel->epg.size(); iEntryPtr < iEntrymax; iEntryPtr++)
		{
			PVRIptvEpgEntry &myTag = egpChannel->epg.at(iEntryPtr);

			if (myTag.endTime < iStart) 
				continue;

			EPG_TAG tag;
			memset(&tag, 0, sizeof(EPG_TAG));

			tag.iUniqueBroadcastId = myTag.iBroadcastId;
			tag.strTitle           = myTag.strTitle.c_str();
			tag.iChannelNumber     = myTag.iChannelId;
			tag.startTime          = myTag.startTime;
			tag.endTime            = myTag.endTime;
			tag.strPlotOutline     = myTag.strPlotOutline.c_str();
			tag.strPlot            = myTag.strPlot.c_str();
			tag.strIconPath        = myTag.strIconPath.c_str();
			tag.iGenreType         = myTag.iGenreType;
			tag.iGenreSubType      = myTag.iGenreSubType;

			PVR->TransferEpgEntry(handle, &tag);

			if (myTag.startTime > iEnd)
				break;
		}

		return PVR_ERROR_NO_ERROR;
	}

	return PVR_ERROR_NO_ERROR;
}

int PVRIptvData::GetRecordingsAmount(void)
{
  return m_recordings.size();
}

PVR_ERROR PVRIptvData::GetRecordings(ADDON_HANDLE handle)
{
  for (std::vector<PVRIptvRecording>::iterator it = m_recordings.begin() ; it != m_recordings.end() ; it++)
  {
    PVRIptvRecording &recording = *it;

    PVR_RECORDING xbmcRecording;

    xbmcRecording.iDuration     = recording.iDuration;
    xbmcRecording.iGenreType    = recording.iGenreType;
    xbmcRecording.iGenreSubType = recording.iGenreSubType;
    xbmcRecording.recordingTime = recording.recordingTime;

    strncpy(xbmcRecording.strChannelName, recording.strChannelName.c_str(), sizeof(xbmcRecording.strChannelName) - 1);
    strncpy(xbmcRecording.strPlotOutline, recording.strPlotOutline.c_str(), sizeof(xbmcRecording.strPlotOutline) - 1);
    strncpy(xbmcRecording.strPlot,        recording.strPlot.c_str(),        sizeof(xbmcRecording.strPlot) - 1);
    strncpy(xbmcRecording.strRecordingId, recording.strRecordingId.c_str(), sizeof(xbmcRecording.strRecordingId) - 1);
    strncpy(xbmcRecording.strTitle,       recording.strTitle.c_str(),       sizeof(xbmcRecording.strTitle) - 1);
    strncpy(xbmcRecording.strStreamURL,   recording.strStreamURL.c_str(),   sizeof(xbmcRecording.strStreamURL) - 1);

    PVR->TransferRecordingEntry(handle, &xbmcRecording);
  }

  return PVR_ERROR_NO_ERROR;
}

CStdString PVRIptvData::GetFileContents(CStdString& url)
{
  CStdString strResult;
  void* fileHandle = XBMC->OpenFile(url.c_str(), 0);

  if (fileHandle)
  {
    char buffer[1024];
    while (int bytesRead = XBMC->ReadFile(fileHandle, buffer, 1024))
      strResult.append(buffer, bytesRead);
    XBMC->CloseFile(fileHandle);
  }
  return strResult;
}

int PVRIptvData::ParseDateTime(CStdString strDate, bool iDateFormat)
{
  struct tm timeinfo;

  memset(&timeinfo, 0, sizeof(tm));

  if (iDateFormat)
    sscanf(strDate, "%04d%02d%02d%02d%02d%02d", &timeinfo.tm_year, &timeinfo.tm_mon, &timeinfo.tm_mday, &timeinfo.tm_hour, &timeinfo.tm_min, &timeinfo.tm_sec);
  else
    sscanf(strDate, "%02d.%02d.%04d%02d:%02d:%02d", &timeinfo.tm_mday, &timeinfo.tm_mon, &timeinfo.tm_year, &timeinfo.tm_hour, &timeinfo.tm_min, &timeinfo.tm_sec);

  timeinfo.tm_mon  -= 1;
  timeinfo.tm_year -= 1900;
  timeinfo.tm_isdst = -1;

  return mktime(&timeinfo);
}

PVRIptvEpgChannel * PVRIptvData::FindEgpChannelById(int iId)
{
	for(int i = 0, max = m_egpChannels.size(); i < max; i++)
	{
		PVRIptvEpgChannel &channel = m_egpChannels.at(i);
		if (channel.iId == iId)
		{
			return &channel;
		}
	}

	return NULL;
}

PVRIptvEpgChannel * PVRIptvData::FindEgpChannelByPvrChannel(PVRIptvChannel &pvrChannel/*, PVRIptvEpgChannel &found*/)
{
	CStdString strName = pvrChannel.strChannelName;
	CStdString strTvgName = pvrChannel.strTvgName;

	for(int i = 0, max = m_egpChannels.size(); i < max; i++)
	{
		PVRIptvEpgChannel &channel = m_egpChannels.at(i);
		if (channel.iId == pvrChannel.iTvgId)
		{
			//found = channel;
			return &channel;
		}

		CStdString strTmpName = channel.strName;
		strTmpName.Replace(' ', '_');

		if (strTvgName.Equals(strTmpName)) {
			//found = channel;
			return &channel;
		}

		strTmpName = channel.strName;

		if (strName.Equals(strTmpName)) {
			//found = channel;
			return &channel;
		}
	}

	return NULL;
}

bool PVRIptvData::gzipInflate( const std::string& compressedBytes, std::string& uncompressedBytes ) {  
  if ( compressedBytes.size() == 0 ) {  
    uncompressedBytes = compressedBytes ;  
    return true ;  
  }  
  
  uncompressedBytes.clear() ;  
  
  unsigned full_length = compressedBytes.size() ;  
  unsigned half_length = compressedBytes.size() / 2;  
  
  unsigned uncompLength = full_length ;  
  char* uncomp = (char*) calloc( sizeof(char), uncompLength );  
  
  z_stream strm;  
  strm.next_in = (Bytef *) compressedBytes.c_str();  
  strm.avail_in = compressedBytes.size() ;  
  strm.total_out = 0;  
  strm.zalloc = Z_NULL;  
  strm.zfree = Z_NULL;  
  
  bool done = false ;  
  
  if (inflateInit2(&strm, (16+MAX_WBITS)) != Z_OK) {  
    free( uncomp );  
    return false;  
  }  
  
  while (!done) {  
    // If our output buffer is too small  
    if (strm.total_out >= uncompLength ) {  
      // Increase size of output buffer  
      char* uncomp2 = (char*) calloc( sizeof(char), uncompLength + half_length );  
      memcpy( uncomp2, uncomp, uncompLength );  
      uncompLength += half_length ;  
      free( uncomp );  
      uncomp = uncomp2 ;  
    }  
  
    strm.next_out = (Bytef *) (uncomp + strm.total_out);  
    strm.avail_out = uncompLength - strm.total_out;  
  
    // Inflate another chunk.  
    int err = inflate (&strm, Z_SYNC_FLUSH);  
    if (err == Z_STREAM_END) done = true;  
    else if (err != Z_OK)  {  
      break;  
    }  
  }  
  
  if (inflateEnd (&strm) != Z_OK) {  
    free( uncomp );  
    return false;  
  }  
  
  for ( size_t i=0; i<strm.total_out; ++i ) {  
    uncompressedBytes += uncomp[ i ];  
  }  
  free( uncomp );  
  return true ;  
}  

CStdString PVRIptvData::GetCachedFileContents(const char * strCachedName, const char * filePath)
{
	CStdString strCachedPath = GetUserFilePath(strCachedName);
	CStdString strFilePath = filePath;
	bool bNeedReload = false;

	if (XBMC->FileExists(strCachedPath, false)) {
		struct __stat64 statCached;
		struct __stat64 statOrig;

		XBMC->StatFile(strCachedPath.c_str(), &statCached);
		XBMC->StatFile(strFilePath.c_str(), &statOrig);

		if (statCached.st_mtime < statOrig.st_mtime) {
			bNeedReload = true;
		}

	} else {
		bNeedReload = true;
	}

	CStdString strResult;
	if (bNeedReload) {
	  strResult = GetFileContents(strFilePath);

	  void* fileHandle = XBMC->OpenFileForWrite(strCachedPath.c_str(), true);

      if (fileHandle)
      {
          XBMC->WriteFile(fileHandle, strResult.c_str(), strResult.length());
          XBMC->CloseFile(fileHandle);
	  }
	} else {
		strResult = GetFileContents(strCachedPath);
	}

    return strResult;
}