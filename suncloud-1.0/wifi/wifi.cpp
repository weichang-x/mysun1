/************
 *
 *功能:
 *
 *
 *
 **/
/*
#ifdef CONFIG_NATIVE_WINDOWS
#include <windows.h>
#endif  CONFIG_NATIVE_WINDOWS */

#include <cstdio>
#include <unistd.h>
#include <QMessageBox>
#include <QCloseEvent>
#include <QSettings>

#include "dirent.h"
#include "common/wpa_ctrl.h"
#include "networkconfig.h"
#include "supplicant.h"

#ifndef QT_NO_DEBUG
#define debug(M, ...) qDebug("DEBUG %d: " M, __LINE__, ##__VA_ARGS__)
#else
#define debug(M, ...) do {} while (0)
#endif

#include "wifi.h"

/************
 *
 *功能:Wifi功能模块的构造函数，检测无线网卡设备工作状态，进行一些UI布局的设置。
 *
 *
 *
 **/
Wifi::Wifi(QWidget *parent) :
    QDialog(parent)
{

        
    QPalette palette;

        QFile file( "/sys/class/net/wlan0/operstate" );
    if ( file.open(QIODevice::ReadOnly | QIODevice::Text) ) {
        QByteArray  line = file.readLine();
        QString mg = line;
        file.close();
        qDebug() << mg;
        if(mg.compare("down\n") == 0)
        {

            QProcess::execute("ifconfig wlan0  up");
        }
    }

    connectedname = "";
	ctrl_iface = strdup("wlan0");
	ctrl_conn = NULL;
	monitor_conn = NULL;
	msgNotifier = NULL;
	scanres = NULL;
	ctrl_iface_dir = strdup("/var/run/wpa_supplicant");
    QVBoxLayout   *layout = new  QVBoxLayout(this);
    QHBoxLayout   *btnlayout = new  QHBoxLayout();

    //networkSelect = new QComboBox();
    networkList = new QListWidget();
    //networkSelect->clear();
    networkList->clear();
    networklist.clear();
    scanbtn = new  QPushButton("搜索");
    scanbtn->setFocusPolicy(Qt::NoFocus);
    //connectbtn = new  QPushButton("Connect");
    disconnectbtn = new  QPushButton("断开");
    disconnectbtn->setFocusPolicy(Qt::NoFocus);


    //palette.setColor(QPalette::ButtonText,Qt::white);
    palette.setColor(QPalette::Button,QColor(26,112,178,220));
    disconnectbtn->setPalette(palette);
    disconnectbtn->setAutoFillBackground(true);
    disconnectbtn->setFlat(true);
    scanbtn->setPalette(palette);
    scanbtn->setAutoFillBackground(true);
    scanbtn->setFlat(true);


    textStatus = new  QLabel();
    //textBssid= new  QLabel();
    //textSsid= new  QLabel();
    //textIpAddress= new  QLabel();
    //textAuthentication= new  QLabel();
    //textEncryption= new  QLabel();
    
    layout->addWidget(textStatus );
    //layout->addWidget(textBssid );
    //layout->addWidget(textSsid );
    //layout->addWidget(networkSelect);
    layout->addWidget(networkList);
    //layout->addWidget(textIpAddress);
    //layout->addWidget(textAuthentication);
    //layout->addWidget(textEncryption);
    btnlayout->addWidget(scanbtn);
    //btnlayout->addWidget(connectbtn);
    btnlayout->addWidget(disconnectbtn);
    layout->addLayout(btnlayout);
    timer = new QTimer(this);
	connect(timer, SIGNAL(timeout()), SLOT(ping()));
	timer->setSingleShot(false);
	timer->start(1000);
    
    signalMeterTimer = new QTimer(this);
	signalMeterTimer->setInterval(signalMeterInterval);
	connect(signalMeterTimer, SIGNAL(timeout()), SLOT(signalMeterUpdate()));
	connect(scanbtn, SIGNAL(clicked()), this, SLOT(scan()));
	//connect(connectbtn, SIGNAL(clicked()), this, SLOT(connectB()));
	//connect(networkSelect, SIGNAL(activated(const QString&)), this,
	//	SLOT(selectNetwork(const QString&)));
    connect(disconnectbtn, SIGNAL(clicked()), this, SLOT(disconnect()));

    //connect(this, SIGNAL(signals_callconfig(QString)), scanres, SLOT(getItem(QString)));
    connect(networkList,SIGNAL(doubleClicked(QModelIndex)),this,SLOT(LinkWifiSlots()));


#if 1
    if (openCtrlConnection(ctrl_iface) < 0) {
		debug("Failed to open control connection to "
		      "wpa_supplicant.");
        connectedname = "";
        emit  signals_update();

	}
    else
    debug("success to open control connection to "
		      "wpa_supplicant.");
#endif
    updateStatus();
	networkMayHaveChanged = true;
	updateNetworks();


}
void    Wifi::LinkWifiSlots()
{
    bool  flag = false;
    QString wifname = networkList->currentItem()->text();
    for(int i=0;i<networklist.size();i++)
    {
        if(networklist.at(i).split(":").at(1).compare(wifname) == 0)
        {
        qDebug()<<wifname<<networklist.at(i).split(":").at(1)<<networklist.at(i).split(":").at(0);
            flag = true;
            selectNetwork(networklist.at(i));
            return;
        }
    }

    if(flag == false)
    {
        textStatus->setText(wifname);
		QString ssid, bssid,flags;
        if(items.size() > 0)
        {
        foreach(QString str,items)
        {
            if(str.split("\t").at(0).compare(wifname) == 0)
            {
               ssid =  str.split("\t").at(0);
               bssid = str.split("\t").at(1);
               flags = str.split("\t").at(2);
               break;
            }
        }
        
        
        NetworkConfig *nc = new NetworkConfig();
	    if (nc == NULL)
		    return;
	    nc->setWpaGui(this);
	    nc->paramsFromScanResults(ssid,flags,bssid);
	    nc->show();
	    nc->exec();
        for(int i=0;i<networklist.size();i++)
        {
            if(networklist.at(i).split(":").at(1).compare(wifname) == 0)
            {
                flag = true;
                selectNetwork(networklist.at(i));
                return;
            }
        }

        }
    }
}
Wifi::~Wifi()
{
    delete msgNotifier;

	if (monitor_conn) {
		wpa_ctrl_detach(monitor_conn);
		wpa_ctrl_close(monitor_conn);
		monitor_conn = NULL;
	}
	if (ctrl_conn) {
		wpa_ctrl_close(ctrl_conn);
		ctrl_conn = NULL;
	}
    if (scanres) {
		delete scanres;
		scanres = NULL;
	}

    free(ctrl_iface);
	ctrl_iface = NULL;

	free(ctrl_iface_dir);
	ctrl_iface_dir = NULL;
}

int Wifi::openCtrlConnection(const char *ifname)
{
	char *cfile;
	int flen;
	char buf[2048], *pos, *pos2;
	size_t len;

	if (ifname) {
		if (ifname != ctrl_iface) {
			free(ctrl_iface);
			ctrl_iface = strdup(ifname);
		}
	} else {
#ifdef CONFIG_CTRL_IFACE_UDP
		free(ctrl_iface);
		ctrl_iface = strdup("udp");
#endif /* CONFIG_CTRL_IFACE_UDP */
#ifdef CONFIG_CTRL_IFACE_UNIX
		struct dirent *dent;
		DIR *dir = opendir(ctrl_iface_dir);
		free(ctrl_iface);
		ctrl_iface = NULL;
		if (dir) {
			while ((dent = readdir(dir))) {
#ifdef _DIRENT_HAVE_D_TYPE
				/* Skip the file if it is not a socket.
				 * Also accept DT_UNKNOWN (0) in case
				 * the C library or underlying file
				 * system does not support d_type. */
				if (dent->d_type != DT_SOCK &&
				    dent->d_type != DT_UNKNOWN)
					continue;
#endif /* _DIRENT_HAVE_D_TYPE */

				if (strcmp(dent->d_name, ".") == 0 ||
				    strcmp(dent->d_name, "..") == 0)
					continue;
				debug("Selected interface '%s'",
				      dent->d_name);
				ctrl_iface = strdup(dent->d_name);
				break;
			}
			closedir(dir);
		}
#endif /* CONFIG_CTRL_IFACE_UNIX */
#ifdef CONFIG_CTRL_IFACE_NAMED_PIPE
		struct wpa_ctrl *ctrl;
		int ret;

		free(ctrl_iface);
		ctrl_iface = NULL;

		ctrl = wpa_ctrl_open(NULL);
		if (ctrl) {
			len = sizeof(buf) - 1;
			ret = wpa_ctrl_request(ctrl, "INTERFACES", 10, buf,
					       &len, NULL);
			if (ret >= 0) {
				connectedToService = true;
				buf[len] = '\0';
				pos = strchr(buf, '\n');
				if (pos)
					*pos = '\0';
				ctrl_iface = strdup(buf);
			}
			wpa_ctrl_close(ctrl);
		}
#endif /* CONFIG_CTRL_IFACE_NAMED_PIPE */
	}

	if (ctrl_iface == NULL) {
#ifdef CONFIG_NATIVE_WINDOWS
		static bool first = true;
		if (first && !serviceRunning()) {
			first = false;
			if (QMessageBox::warning(
				    this, qAppName(),
				    tr("wpa_supplicant service is not "
				       "running.\n"
				       "Do you want to start it?"),
				    QMessageBox::Yes | QMessageBox::No) ==
			    QMessageBox::Yes)
				startService();
		}
#endif /* CONFIG_NATIVE_WINDOWS */
		return -1;
	}

#ifdef CONFIG_CTRL_IFACE_UNIX
	flen = strlen(ctrl_iface_dir) + strlen(ctrl_iface) + 2;
	cfile = (char *) malloc(flen);
	if (cfile == NULL)
		return -1;
	snprintf(cfile, flen, "%s/%s", ctrl_iface_dir, ctrl_iface);
#else /* CONFIG_CTRL_IFACE_UNIX */
	flen = strlen(ctrl_iface) + 1;
	cfile = (char *) malloc(flen);
	if (cfile == NULL)
		return -1;
	snprintf(cfile, flen, "%s", ctrl_iface);
#endif /* CONFIG_CTRL_IFACE_UNIX */

	if (ctrl_conn) {
		wpa_ctrl_close(ctrl_conn);
		ctrl_conn = NULL;
	}

	if (monitor_conn) {
		delete msgNotifier;
		msgNotifier = NULL;
		wpa_ctrl_detach(monitor_conn);
		wpa_ctrl_close(monitor_conn);
		monitor_conn = NULL;
	}

	debug("Trying to connect to '%s'", cfile);
	ctrl_conn = wpa_ctrl_open(cfile);
	if (ctrl_conn == NULL) {
		free(cfile);
		return -1;
	}
	monitor_conn = wpa_ctrl_open(cfile);
	free(cfile);
	if (monitor_conn == NULL) {
		wpa_ctrl_close(ctrl_conn);
		return -1;
	}
	if (wpa_ctrl_attach(monitor_conn)) {
		debug("Failed to attach to wpa_supplicant");
		wpa_ctrl_close(monitor_conn);
		monitor_conn = NULL;
		wpa_ctrl_close(ctrl_conn);
		ctrl_conn = NULL;
		return -1;
	}

#if defined(CONFIG_CTRL_IFACE_UNIX) || defined(CONFIG_CTRL_IFACE_UDP)
	msgNotifier = new QSocketNotifier(wpa_ctrl_get_fd(monitor_conn),
					  QSocketNotifier::Read, this);
	connect(msgNotifier, SIGNAL(activated(int)), SLOT(receiveMsgs()));
#endif


	len = sizeof(buf) - 1;
	if (wpa_ctrl_request(ctrl_conn, "INTERFACES", 10, buf, &len, NULL) >=
	    0) {
		buf[len] = '\0';
		pos = buf;
		while (*pos) {
			pos2 = strchr(pos, '\n');
			if (pos2)
				*pos2 = '\0';
			//if (strcmp(pos, ctrl_iface) != 0)
			//	textSsid->setText(tr("adapter:")+pos);//adapterSelect->addItem(pos);
			if (pos2)
				pos = pos2 + 1;
			else
				break;
		}
	}

	len = sizeof(buf) - 1;
	if (wpa_ctrl_request(ctrl_conn, "GET_CAPABILITY eap", 18, buf, &len,
			     NULL) >= 0) {
		buf[len] = '\0';

		QString res(buf);
        debug(res);
		//QStringList types = res.split(QChar(' '));
		//bool wps = types.contains("WSC");
		//actionWPS->setEnabled(wps);
		//wpsTab->setEnabled(wps);
		//wpaguiTab->setTabEnabled(wpaguiTab->indexOf(wpsTab), wps);
	}

	return 0;
}


int Wifi::ctrlRequest(const char *cmd, char *buf, size_t *buflen)
{
	int ret;

	if (ctrl_conn == NULL)
		return -3;
	ret = wpa_ctrl_request(ctrl_conn, cmd, strlen(cmd), buf, buflen, NULL);
	if (ret == -2)
		debug("'%s' command timed out.", cmd);
	else if (ret < 0)
		debug("'%s' command failed.", cmd);

	return ret;
}

QString Wifi::wpaStateTranslate(char *state)
{
    if (!strcmp(state, "DISCONNECTED"))
		return tr("Disconnected");
	else if (!strcmp(state, "INACTIVE"))
		return tr("Inactive");
	else if (!strcmp(state, "SCANNING"))
		return tr("Scanning");
	else if (!strcmp(state, "AUTHENTICATING"))
		return tr("Authenticating");
	else if (!strcmp(state, "ASSOCIATING"))
		return tr("Associating");
	else if (!strcmp(state, "ASSOCIATED"))
		return tr("Associated");
	else if (!strcmp(state, "4WAY_HANDSHAKE"))
    {
        for(int i=0;i<networklist.size();i++)
        {
        if(networklist.at(i).split(":").at(1).compare(connectedname) == 0)
        {
                removeNetwork(networklist.at(i));
                connectedname="";
                break;
        }
        }
		return tr("4-Way Handshake");
	}
    else if (!strcmp(state, "GROUP_HANDSHAKE"))
		return tr("Group Handshake");
	else if (!strcmp(state, "COMPLETED"))
    {
        		return tr("Completed");
	}
    else
		return tr("Unknown");
}

void Wifi::updateStatus()
{
	char buf[2048], *start, *end, *pos;
	size_t len;

    connectedname = "";
	pingsToStatusUpdate = 10;

	len = sizeof(buf) - 1;
	if (ctrl_conn == NULL || ctrlRequest("STATUS", buf, &len) < 0) {
		textStatus->setText(tr("Could not get status from "
				       "wpa_supplicant"));
        DeamonThread  *server = new  DeamonThread("wlan0","/root/wpa_supplicant.conf");
        server->start();
        emit  signals_update();

		//textAuthentication->clear();
		//textEncryption->clear();
		//textSsid->clear();
		//textBssid->clear();
		//textIpAddress->clear();
		signalMeterTimer->stop();

#ifdef CONFIG_NATIVE_WINDOWS
		static bool first = true;
		if (first && connectedToService &&
		    (ctrl_iface == NULL || *ctrl_iface == '\0')) {
			first = false;
			if (QMessageBox::information(
				    this, qAppName(),
				    tr("No network interfaces in use.\n"
				       "Would you like to add one?"),
				    QMessageBox::Yes | QMessageBox::No) ==
			    QMessageBox::Yes)
				addInterface();
		}
#endif /* CONFIG_NATIVE_WINDOWS */
		return;
	}

	buf[len] = '\0';

	bool /*auth_updated = false,*/ ssid_updated = false;
	//bool /*bssid_updated = false,*/ ipaddr_updated = false;
	bool status_updated = false;
	char *pairwise_cipher = NULL, *group_cipher = NULL;
	char *mode = NULL;

	start = buf;
	while (*start) {
		bool last = false;
		end = strchr(start, '\n');
		if (end == NULL) {
			last = true;
			end = start;
			while (end[0] && end[1])
				end++;
		}
		*end = '\0';

		pos = strchr(start, '=');
		if (pos) {
			*pos++ = '\0';
			if (strcmp(start, "bssid") == 0) {
				//bssid_updated = true;
				//textBssid->setText(pos);
			} else if (strcmp(start, "ssid") == 0) {
				ssid_updated = true;
				//textSsid->setText(pos);
                QString name = pos;
#if 1
             int m = name.indexOf("\\x");
             int n = name.lastIndexOf("\\x");
            if(m >= 0)
            {
                QString code = name.mid(m,n-m+4);
                QStringList  hzlist = code.split("\\x");
                int v[hzlist.size()-1];
                char  buffer[hzlist.size()];
                memset(buffer,0,sizeof(buffer));
                for(int i=0;i<hzlist.size()-1;i++)
                {
                    sscanf(hzlist.at(i+1).toLocal8Bit().data(),"%2X",&v[i]);
                } 
                for(int i=0;i<hzlist.size()-1;i++)
                    buffer[i] = (char)v[i];
                qDebug()<<"***buffer:"<<tr(buffer);
                name.replace(m,n-m+4,tr(buffer));
            }
#endif
                connectedname = name;
				if (!signalMeterInterval) {
					/* if signal meter is not enabled show
					 * full signal strength */
				}
			} else if (strcmp(start, "ip_address") == 0) {
                emit  siglogin(99);
				//ipaddr_updated = true;
				//textIpAddress->setText(pos);
			} else if (strcmp(start, "wpa_state") == 0) {
				status_updated = true;
				textStatus->setText(wpaStateTranslate(pos));
			} else if (strcmp(start, "key_mgmt") == 0) {
				//auth_updated = true;
				//textAuthentication->setText(pos);
				/* TODO: could add EAP status to this */
			} else if (strcmp(start, "pairwise_cipher") == 0) {
				pairwise_cipher = pos;
			} else if (strcmp(start, "group_cipher") == 0) {
				group_cipher = pos;
			} else if (strcmp(start, "mode") == 0) {
				mode = pos;
			}
		}

		if (last)
			break;
		start = end + 1;
	}
	if (status_updated && mode)
		textStatus->setText(textStatus->text() + " (" + mode + ")");

	if (pairwise_cipher || group_cipher) {
		QString encr;
		if (pairwise_cipher && group_cipher &&
		    strcmp(pairwise_cipher, group_cipher) != 0) {
			encr.append(pairwise_cipher);
			encr.append(" + ");
			encr.append(group_cipher);
		} else if (pairwise_cipher) {
			encr.append(pairwise_cipher);
		} else {
			encr.append(group_cipher);
			encr.append(" [group key only]");
		}
		//textEncryption->setText(encr);
	} //else
		//textEncryption->clear();

    emit  signals_update();
	if (signalMeterInterval) {
		/*
		 * Handle signal meter service. When network is not associated,
		 * deactivate timer, otherwise keep it going. Tray icon has to
		 * be initialized here, because of the initial delay of the
		 * timer.
		 */
		if (ssid_updated) {
			if (!signalMeterTimer->isActive()) {
				//updateTrayIcon(TrayIconConnected);
				signalMeterTimer->start();
			}
		} else {
			signalMeterTimer->stop();
		}
	}

    if (!status_updated)
		textStatus->clear();
	//if (!auth_updated)
	//	textAuthentication->clear();
	//if (!ssid_updated) {
	//	textSsid->clear();
	//}
	//if (!bssid_updated)
	//	textBssid->clear();
	//if (!ipaddr_updated)
	//	textIpAddress->clear();
}


void Wifi::updateNetworks()
{
	char buf[4096], *start, *end, *id, *ssid, *bssid, *flags;
	size_t len;
	int first_active = -1;
	int was_selected = -1;
	bool current = false;

	if (!networkMayHaveChanged)
		return;

	if (networkList->currentRow() >= 0)
		was_selected = networkList->currentRow();

	//networkSelect->clear();
	networklist.clear();

	if (ctrl_conn == NULL)
		return;

	len = sizeof(buf) - 1;
	if (ctrlRequest("LIST_NETWORKS", buf, &len) < 0)
		return;

	buf[len] = '\0';
	start = strchr(buf, '\n');
	if (start == NULL)
		return;
	start++;

	while (*start) {
		bool last = false;
		end = strchr(start, '\n');
		if (end == NULL) {
			last = true;
			end = start;
			while (end[0] && end[1])
				end++;
		}
		*end = '\0';

		id = start;
		ssid = strchr(id, '\t');
		if (ssid == NULL)
			break;
		*ssid++ = '\0';
		bssid = strchr(ssid, '\t');
		if (bssid == NULL)
			break;
		*bssid++ = '\0';
		flags = strchr(bssid, '\t');
		if (flags == NULL)
			break;
		*flags++ = '\0';

		if (strstr(flags, "[DISABLED][P2P-PERSISTENT]")) {
			if (last)
				break;
			start = end + 1;
			continue;
		}

		QString network(id);
		network.append(":");
		network.append(ssid);
#if 1
             int m = network.indexOf("\\x");
             int n = network.lastIndexOf("\\x");
            if(m >= 0)
            {
                QString code = network.mid(m,n-m+4);
                QStringList  hzlist = code.split("\\x");
                int v[hzlist.size()-1];
                char  buffer[hzlist.size()];
                memset(buffer,0,sizeof(buffer));
                for(int i=0;i<hzlist.size()-1;i++)
                {
                    sscanf(hzlist.at(i+1).toLocal8Bit().data(),"%2X",&v[i]);
                } 
                for(int i=0;i<hzlist.size()-1;i++)
                    buffer[i] = (char)v[i];
                qDebug()<<"***buffer:"<<tr(buffer);
                network.replace(m,n-m+4,tr(buffer));
            }
#endif

		//networkSelect->addItem(network);
		networklist.push_back(network);

		if (strstr(flags, "[CURRENT]")) {
			//networkSelect->setCurrentIndex(networkSelect->count() -
		    //				      1);
            connectedname = networklist.at(networklist.size()-1).split(":").at(1);
			current = true;
		} else if (first_active < 0 &&
			   strstr(flags, "[DISABLED]") == NULL)
			//first_active = networkSelect->count() - 1;
			first_active = networklist.size() - 1;

		if (last)
			break;
		start = end + 1;
	}

	//if (networkSelect->count() > 1)
	//	networkSelect->addItem(tr("Select any network"));

	//if (!current && first_active >= 0)
	//	networkSelect->setCurrentIndex(first_active);

	if (was_selected >= 0 && networkList->count() > 0) {
		if (was_selected < networkList->count())
			networkList->setCurrentRow(was_selected);
		else
			networkList->setCurrentRow(networkList->count() - 1);
	}
	//else
	//	networkList->setCurrentRow(networkSelect->currentIndex());

	networkMayHaveChanged = false;
}

void Wifi::disconnect()
{
	char reply[10];
	size_t reply_len = sizeof(reply);
	ctrlRequest("DISCONNECT", reply, &reply_len);
}

void Wifi::scan()
{
    
	if (scanres) {
		delete scanres;
	}

	scanres = new ScanResults();
	
    if (scanres == NULL)
		return;
	scanres->setWpaGui(this,networkList,&items);
        qDebug()<<"++++++++++++++++++++";
    foreach(QString str,items)
    {
        qDebug()<<str;
    }
}
void Wifi::ping()
{
	char buf[10];
	size_t len;

#ifdef CONFIG_CTRL_IFACE_NAMED_PIPE
	/*
	 * QSocketNotifier cannot be used with Windows named pipes, so use a
	 * timer to check for received messages for now. This could be
	 * optimized be doing something specific to named pipes or Windows
	 * events, but it is not clear what would be the best way of doing that
	 * in Qt.
	 */
	receiveMsgs();
#endif /* CONFIG_CTRL_IFACE_NAMED_PIPE */

    if (scanres /*&& !scanres->isVisible()*/) {
		delete scanres;
		scanres = NULL;
	}
	len = sizeof(buf) - 1;
	if (ctrlRequest("PING", buf, &len) < 0) {
		debug("PING failed - trying to reconnect");
		if (openCtrlConnection(ctrl_iface) >= 0) {
			debug("Reconnected successfully");
			pingsToStatusUpdate = 0;
		}
	}

	pingsToStatusUpdate--;
	if (pingsToStatusUpdate <= 0) {
		updateStatus();
		updateNetworks();
	}

#ifndef CONFIG_CTRL_IFACE_NAMED_PIPE
	/* Use less frequent pings and status updates when the main window is
	 * hidden (running in taskbar). */
	int interval = isHidden() ? 5000 : 1000;
	if (timer->interval() != interval)
		timer->setInterval(interval);
#endif /* CONFIG_CTRL_IFACE_NAMED_PIPE */
}
#if 1
void Wifi::signalMeterUpdate()
{
	char reply[128];
	size_t reply_len = sizeof(reply);
	char *rssi;
	//int rssi_value;

	ctrlRequest("SIGNAL_POLL", reply, &reply_len);

	/* In order to eliminate signal strength fluctuations, try
	 * to obtain averaged RSSI value in the first place. */
	//if ((rssi = strstr(reply, "AVG_RSSI=")) != NULL)
	//	rssi_value = atoi(&rssi[sizeof("AVG_RSSI")]);
	//else if ((rssi = strstr(reply, "RSSI=")) != NULL)
	//	rssi_value = atoi(&rssi[sizeof("RSSI")]);
	//else {
	//	debug("Failed to get RSSI value");
	//	return;
	//}

	//debug("RSSI value: %d", rssi_value);

	/*
	 * NOTE: The code below assumes, that the unit of the value returned
	 * by the SIGNAL POLL request is dBm. It might not be true for all
	 * wpa_supplicant drivers.
	 */

	/*
	 * Calibration is based on "various Internet sources". Nonetheless,
	 * it seems to be compatible with the Windows 8.1 strength meter -
	 * tested on Intel Centrino Advanced-N 6235.
	 */
#if 0
	if (rssi_value >= -60)
		updateTrayIcon(TrayIconSignalExcellent);
	else if (rssi_value >= -68)
		updateTrayIcon(TrayIconSignalGood);
	else if (rssi_value >= -76)
		updateTrayIcon(TrayIconSignalOk);
	else if (rssi_value >= -84)
		updateTrayIcon(TrayIconSignalWeak);
	else
		updateTrayIcon(TrayIconSignalNone);
#endif
}
#endif

static int str_match(const char *a, const char *b)
{
	return strncmp(a, b, strlen(b)) == 0;
}


void Wifi::processMsg(char *msg)
{
	char *pos = msg, *pos2;
	int priority = 2;

	if (*pos == '<') {
		/* skip priority */
		pos++;
		priority = atoi(pos);
		pos = strchr(pos, '>');
		if (pos)
			pos++;
		else
			pos = msg;
	}

	WpaMsg wm(pos, priority);
	//if (eh)
	//	eh->addEvent(wm);
	//if (peers)
	//	peers->event_notify(wm);
	msgs.append(wm);
	while (msgs.count() > 100)
		msgs.pop_front();
	/* Update last message with truncated version of the event */
	if (strncmp(pos, "CTRL-", 5) == 0) {
		pos2 = strchr(pos, str_match(pos, WPA_CTRL_REQ) ? ':' : ' ');
		if (pos2)
			pos2++;
		else
			pos2 = pos;
	} else
		pos2 = pos;
	QString lastmsg = pos2;
	lastmsg.truncate(40);
	//textLastMessage->setText(lastmsg);
    qDebug()<<lastmsg;
	pingsToStatusUpdate = 0;
	//networkMayHaveChanged = true;
    if (str_match(pos, WPA_EVENT_SCAN_RESULTS) && scanres)
		scanres->updateResults();

#if 0
	if (str_match(pos, WPA_CTRL_REQ))
		processCtrlReq(pos + strlen(WPA_CTRL_REQ));
	else if (str_match(pos, WPA_EVENT_SCAN_RESULTS) && scanres)
		scanres->updateResults();
	else if (str_match(pos, WPA_EVENT_DISCONNECTED))
		showTrayMessage(QSystemTrayIcon::Information, 3,
				tr("Disconnected from network."));
	else if (str_match(pos, WPA_EVENT_CONNECTED)) {
		showTrayMessage(QSystemTrayIcon::Information, 3,
				tr("Connection to network established."));
		QTimer::singleShot(5 * 1000, this, SLOT(showTrayStatus()));
		stopWpsRun(true);
	} else if (str_match(pos, WPS_EVENT_AP_AVAILABLE_PBC)) {
		//wpsStatusText->setText(tr("WPS AP in active PBC mode found"));
		if (textStatus->text() == "INACTIVE" ||
		    textStatus->text() == "DISCONNECTED")
			wpaguiTab->setCurrentWidget(wpsTab);
		wpsInstructions->setText(tr("Press the PBC button on the "
					    "screen to start registration"));
	} else if (str_match(pos, WPS_EVENT_AP_AVAILABLE_PIN)) {
		//wpsStatusText->setText(tr("WPS AP with recently selected "
		//			  "registrar"));
		if (textStatus->text() == "INACTIVE" ||
		    textStatus->text() == "DISCONNECTED")
			wpaguiTab->setCurrentWidget(wpsTab);
	} else if (str_match(pos, WPS_EVENT_AP_AVAILABLE_AUTH)) {
		showTrayMessage(QSystemTrayIcon::Information, 3,
				"Wi-Fi Protected Setup (WPS) AP\n"
				"indicating this client is authorized.");
		//wpsStatusText->setText("WPS AP indicating this client is "
		//		       "authorized");
		if (textStatus->text() == "INACTIVE" ||
		    textStatus->text() == "DISCONNECTED")
			wpaguiTab->setCurrentWidget(wpsTab);
	} else if (str_match(pos, WPS_EVENT_AP_AVAILABLE)) {
		//wpsStatusText->setText(tr("WPS AP detected"));
	} else if (str_match(pos, WPS_EVENT_OVERLAP)) {
		//wpsStatusText->setText(tr("PBC mode overlap detected"));
		wpsInstructions->setText(tr("More than one AP is currently in "
					    "active WPS PBC mode. Wait couple "
					    "of minutes and try again"));
		wpaguiTab->setCurrentWidget(wpsTab);
	} else if (str_match(pos, WPS_EVENT_CRED_RECEIVED)) {
		//wpsStatusText->setText(tr("Network configuration received"));
		wpaguiTab->setCurrentWidget(wpsTab);
	} else if (str_match(pos, WPA_EVENT_EAP_METHOD)) {
		if (strstr(pos, "(WSC)"))
			;//wpsStatusText->setText(tr("Registration started"));
	} else if (str_match(pos, WPS_EVENT_M2D)) {
		//wpsStatusText->setText(tr("Registrar does not yet know PIN"));
	} else if (str_match(pos, WPS_EVENT_FAIL)) {
		//wpsStatusText->setText(tr("Registration failed"));
	} else if (str_match(pos, WPS_EVENT_SUCCESS)) {
		//wpsStatusText->setText(tr("Registration succeeded"));
	}
#endif
}


#if 0
void Wifi::processCtrlReq(const char *req)
{
	if (udr) {
		udr->close();
		delete udr;
	}
	udr = new UserDataRequest();
	if (udr == NULL)
		return;
	if (udr->setParams(this, req) < 0) {
		delete udr;
		udr = NULL;
		return;
	}
	udr->show();
	udr->exec();
}

#endif
void Wifi::receiveMsgs()
{
	char buf[256];
	size_t len;

	while (monitor_conn && wpa_ctrl_pending(monitor_conn) > 0) {
		len = sizeof(buf) - 1;
		if (wpa_ctrl_recv(monitor_conn, buf, &len) == 0) {
			buf[len] = '\0';
			processMsg(buf);
		}
	}
}
void Wifi::connectB()
{
	char reply[10];
	size_t reply_len = sizeof(reply);
	ctrlRequest("REASSOCIATE", reply, &reply_len);
}
void Wifi::selectNetwork( const QString &sel )
{
	QString cmd(sel);
	char reply[10];
	size_t reply_len = sizeof(reply);

	if (cmd.contains(QRegExp("^\\d+:")))
		cmd.truncate(cmd.indexOf(':'));
	else
		cmd = "any";
	cmd.prepend("SELECT_NETWORK ");
    qDebug()<<"cmd******:"<<cmd;
	ctrlRequest(cmd.toLocal8Bit().constData(), reply, &reply_len);
	triggerUpdate();
	//stopWpsRun(false);
}


#if 0
void Wifi::enableNetwork(const QString &sel)
{
	QString cmd(sel);
	char reply[10];
	size_t reply_len = sizeof(reply);

	if (cmd.contains(QRegExp("^\\d+:")))
		cmd.truncate(cmd.indexOf(':'));
	else if (!cmd.startsWith("all")) {
		debug("Invalid editNetwork '%s'",
		      cmd.toLocal8Bit().constData());
		return;
	}
	cmd.prepend("ENABLE_NETWORK ");
	ctrlRequest(cmd.toLocal8Bit().constData(), reply, &reply_len);
	triggerUpdate();
}


void Wifi::disableNetwork(const QString &sel)
{
	QString cmd(sel);
	char reply[10];
	size_t reply_len = sizeof(reply);

	if (cmd.contains(QRegExp("^\\d+:")))
		cmd.truncate(cmd.indexOf(':'));
	else if (!cmd.startsWith("all")) {
		debug("Invalid editNetwork '%s'",
		      cmd.toLocal8Bit().constData());
		return;
	}
	cmd.prepend("DISABLE_NETWORK ");
	ctrlRequest(cmd.toLocal8Bit().constData(), reply, &reply_len);
	triggerUpdate();
}

#endif
void Wifi::editNetwork(const QString &sel)
{
	QString cmd(sel);
	int id = -1;

	if (cmd.contains(QRegExp("^\\d+:"))) {
		cmd.truncate(cmd.indexOf(':'));
		id = cmd.toInt();
	}

	NetworkConfig *nc = new NetworkConfig();
	if (nc == NULL)
		return;

	if (id >= 0)
		nc->paramsFromConfig(id);
	else
		nc->newNetwork();

	nc->show();
	nc->exec();
}
#if 0

void Wifi::editSelectedNetwork()
{
	if (networkSelect->count() < 1) {
		QMessageBox::information(
			this, tr("No Networks"),
			tr("There are no networks to edit.\n"));
		return;
	}
	QString sel(networkSelect->currentText());
	editNetwork(sel);
}


void Wifi::editListedNetwork()
{
	if (networkList->currentRow() < 0) {
		QMessageBox::information(this, tr("Select A Network"),
					 tr("Select a network from the list to"
					    " edit it.\n"));
		return;
	}
	QString sel(networkList->currentItem()->text());
	editNetwork(sel);
}

#endif
void Wifi::triggerUpdate()
{
	updateStatus();
	networkMayHaveChanged = true;
	updateNetworks();
}

void Wifi::addNetwork()
{
	NetworkConfig *nc = new NetworkConfig();
	if (nc == NULL)
		return;
	nc->newNetwork();
	nc->show();
	nc->exec();
}


void Wifi::removeNetwork(const QString &sel)
{
	QString cmd(sel);
	char reply[10];
	size_t reply_len = sizeof(reply);

	if (cmd.contains(QRegExp("^\\d+:")))
		cmd.truncate(cmd.indexOf(':'));
	else if (!cmd.startsWith("all")) {
		debug("Invalid editNetwork '%s'",
		      cmd.toLocal8Bit().constData());
		return;
	}
	cmd.prepend("REMOVE_NETWORK ");
	ctrlRequest(cmd.toLocal8Bit().constData(), reply, &reply_len);
	triggerUpdate();
}

#if 0

void Wifi::removeSelectedNetwork()
{
	if (networkSelect->count() < 1) {
		QMessageBox::information(this, tr("No Networks"),
			                 tr("There are no networks to remove."
					    "\n"));
		return;
	}
	QString sel(networkSelect->currentText());
	removeNetwork(sel);
}


void Wifi::removeListedNetwork()
{
	if (networkList->currentRow() < 0) {
		QMessageBox::information(this, tr("Select A Network"),
					 tr("Select a network from the list "
					    "to remove it.\n"));
		return;
	}
	QString sel(networkList->currentItem()->text());
	removeNetwork(sel);
}


void Wifi::enableAllNetworks()
{
	QString sel("all");
	enableNetwork(sel);
}


void Wifi::disableAllNetworks()
{
	QString sel("all");
	disableNetwork(sel);
}


void Wifi::removeAllNetworks()
{
	QString sel("all");
	removeNetwork(sel);
}


int Wifi::getNetworkDisabled(const QString &sel)
{
	QString cmd(sel);
	char reply[10];
	size_t reply_len = sizeof(reply) - 1;
	int pos = cmd.indexOf(':');
	if (pos < 0) {
		debug("Invalid getNetworkDisabled '%s'",
		      cmd.toLocal8Bit().constData());
		return -1;
	}
	cmd.truncate(pos);
	cmd.prepend("GET_NETWORK ");
	cmd.append(" disabled");

	if (ctrlRequest(cmd.toLocal8Bit().constData(), reply, &reply_len) >= 0
	    && reply_len >= 1) {
		reply[reply_len] = '\0';
		if (!str_match(reply, "FAIL"))
			return atoi(reply);
	}

	return -1;
}


void Wifi::updateNetworkDisabledStatus()
{
	if (networkList->currentRow() < 0)
		return;

	QString sel(networkList->currentItem()->text());

	switch (getNetworkDisabled(sel)) {
	case 0:
		if (!enableRadioButton->isChecked())
			enableRadioButton->setChecked(true);
		return;
	case 1:
		if (!disableRadioButton->isChecked())
			disableRadioButton->setChecked(true);
		return;
	}
}


void Wifi::enableListedNetwork(bool enabled)
{
	if (networkList->currentRow() < 0 || !enabled)
		return;

	QString sel(networkList->currentItem()->text());

	if (getNetworkDisabled(sel) == 1)
		enableNetwork(sel);
}


void Wifi::disableListedNetwork(bool disabled)
{
	if (networkList->currentRow() < 0 || !disabled)
		return;

	QString sel(networkList->currentItem()->text());

	if (getNetworkDisabled(sel) == 0)
		disableNetwork(sel);
}


void Wifi::saveConfig()
{
	char buf[10];
	size_t len;

	len = sizeof(buf) - 1;
	ctrlRequest("SAVE_CONFIG", buf, &len);

	buf[len] = '\0';

	if (str_match(buf, "FAIL"))
		QMessageBox::warning(
			this, tr("Failed to save configuration"),
			tr("The configuration could not be saved.\n"
			   "\n"
			   "The update_config=1 configuration option\n"
			   "must be used for configuration saving to\n"
			   "be permitted.\n"));
	else
		QMessageBox::information(
			this, tr("Saved configuration"),
			tr("The current configuration was saved."
			   "\n"));
}


void Wifi::selectAdapter( const QString & sel )
{
	if (openCtrlConnection(sel.toLocal8Bit().constData()) < 0)
    {
		debug("Failed to open control connection to "
		      "wpa_supplicant.");
        connectedname = "";
        emit  signals_update();
	}
    updateStatus();
	updateNetworks();
}


void Wifi::createTrayIcon(bool trayOnly)
{
	QApplication::setQuitOnLastWindowClosed(false);

	tray_icon = new QSystemTrayIcon(this);
	updateTrayIcon(TrayIconOffline);

	connect(tray_icon,
		SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
		this, SLOT(trayActivated(QSystemTrayIcon::ActivationReason)));

	ackTrayIcon = false;

	tray_menu = new QMenu(this);

	disconnectAction = new QAction(tr("&Disconnect"), this);
	reconnectAction = new QAction(tr("Re&connect"), this);
	connect(disconnectAction, SIGNAL(triggered()), this,
		SLOT(disconnect()));
	connect(reconnectAction, SIGNAL(triggered()), this,
		SLOT(connectB()));
	tray_menu->addAction(disconnectAction);
	tray_menu->addAction(reconnectAction);
	tray_menu->addSeparator();

	eventAction = new QAction(tr("&Event History"), this);
	scanAction = new QAction(tr("Scan &Results"), this);
	statAction = new QAction(tr("S&tatus"), this);
	connect(eventAction, SIGNAL(triggered()), this, SLOT(eventHistory()));
	connect(scanAction, SIGNAL(triggered()), this, SLOT(scan()));
	connect(statAction, SIGNAL(triggered()), this, SLOT(showTrayStatus()));
	tray_menu->addAction(eventAction);
	tray_menu->addAction(scanAction);
	tray_menu->addAction(statAction);
	tray_menu->addSeparator();

	showAction = new QAction(tr("&Show Window"), this);
	hideAction = new QAction(tr("&Hide Window"), this);
	quitAction = new QAction(tr("&Quit"), this);
	connect(showAction, SIGNAL(triggered()), this, SLOT(show()));
	connect(hideAction, SIGNAL(triggered()), this, SLOT(hide()));
	connect(quitAction, SIGNAL(triggered()), qApp, SLOT(quit()));
	tray_menu->addAction(showAction);
	tray_menu->addAction(hideAction);
	tray_menu->addSeparator();
	tray_menu->addAction(quitAction);

	tray_icon->setContextMenu(tray_menu);

	tray_icon->show();

	if (!trayOnly)
		show();
	inTray = trayOnly;
}


void Wifi::showTrayMessage(QSystemTrayIcon::MessageIcon type, int sec,
			     const QString & msg)
{
	if (!QSystemTrayIcon::supportsMessages())
		return;

	if (isVisible() || !tray_icon || !tray_icon->isVisible() || quietMode)
		return;

	tray_icon->showMessage(qAppName(), msg, type, sec * 1000);
}


void Wifi::trayActivated(QSystemTrayIcon::ActivationReason how)
 {
	switch (how) {
	/* use close() here instead of hide() and allow the
	 * custom closeEvent handler take care of children */
	case QSystemTrayIcon::Trigger:
		ackTrayIcon = true;
		if (isVisible()) {
			close();
			inTray = true;
		} else {
			show();
			inTray = false;
		}
		break;
	case QSystemTrayIcon::MiddleClick:
		showTrayStatus();
		break;
	default:
		break;
	}
}


void Wifi::showTrayStatus()
{
	char buf[2048];
	size_t len;

	len = sizeof(buf) - 1;
	if (ctrlRequest("STATUS", buf, &len) < 0)
		return;
	buf[len] = '\0';

	QString msg, status(buf);

	QStringList lines = status.split(QRegExp("\\n"));
	for (QStringList::Iterator it = lines.begin();
	     it != lines.end(); it++) {
		int pos = (*it).indexOf('=') + 1;
		if (pos < 1)
			continue;

		if ((*it).startsWith("bssid="))
			msg.append("BSSID:\t" + (*it).mid(pos) + "\n");
		else if ((*it).startsWith("ssid="))
			msg.append("SSID: \t" + (*it).mid(pos) + "\n");
		else if ((*it).startsWith("pairwise_cipher="))
			msg.append("PAIR: \t" + (*it).mid(pos) + "\n");
		else if ((*it).startsWith("group_cipher="))
			msg.append("GROUP:\t" + (*it).mid(pos) + "\n");
		else if ((*it).startsWith("key_mgmt="))
			msg.append("AUTH: \t" + (*it).mid(pos) + "\n");
		else if ((*it).startsWith("wpa_state="))
			msg.append("STATE:\t" + (*it).mid(pos) + "\n");
		else if ((*it).startsWith("ip_address="))
			msg.append("IP:   \t" + (*it).mid(pos) + "\n");
		else if ((*it).startsWith("Supplicant PAE state="))
			msg.append("PAE:  \t" + (*it).mid(pos) + "\n");
		else if ((*it).startsWith("EAP state="))
			msg.append("EAP:  \t" + (*it).mid(pos) + "\n");
	}

	if (!msg.isEmpty())
		showTrayMessage(QSystemTrayIcon::Information, 10, msg);
}


void Wifi::updateTrayToolTip(const QString &msg)
{
	if (tray_icon)
		tray_icon->setToolTip(msg);
}


void Wifi::updateTrayIcon(TrayIconType type)
{
	if (!tray_icon || currentIconType == type)
		return;

	QIcon fallback_icon;
	QStringList names;

	if (QImageReader::supportedImageFormats().contains(QByteArray("svg")))
		fallback_icon = QIcon(":/icons/wpa_gui.svg");
	else
		fallback_icon = QIcon(":/icons/wpa_gui.png");

	switch (type) {
	case TrayIconOffline:
		names << "network-wireless-offline-symbolic"
		      << "network-wireless-offline"
		      << "network-wireless-signal-none-symbolic"
		      << "network-wireless-signal-none";
		break;
	case TrayIconAcquiring:
		names << "network-wireless-acquiring-symbolic"
		      << "network-wireless-acquiring";
		break;
	case TrayIconConnected:
		names << "network-wireless-connected-symbolic"
		      << "network-wireless-connected";
		break;
	case TrayIconSignalNone:
		names << "network-wireless-signal-none-symbolic"
		      << "network-wireless-signal-none";
		break;
	case TrayIconSignalWeak:
		names << "network-wireless-signal-weak-symbolic"
		      << "network-wireless-signal-weak";
		break;
	case TrayIconSignalOk:
		names << "network-wireless-signal-ok-symbolic"
		      << "network-wireless-signal-ok";
		break;
	case TrayIconSignalGood:
		names << "network-wireless-signal-good-symbolic"
		      << "network-wireless-signal-good";
		break;
	case TrayIconSignalExcellent:
		names << "network-wireless-signal-excellent-symbolic"
		      << "network-wireless-signal-excellent";
		break;
	}

	currentIconType = type;
	tray_icon->setIcon(loadThemedIcon(names, fallback_icon));
}


QIcon Wifi::loadThemedIcon(const QStringList &names,
			     const QIcon &fallback)
{
	QIcon icon;

	for (QStringList::ConstIterator it = names.begin();
	     it != names.end(); it++) {
		icon = QIcon::fromTheme(*it);
		if (!icon.isNull())
			return icon;
	}

	return fallback;
}


void Wifi::closeEvent(QCloseEvent *event)
{
	if (eh) {
		eh->close();
		delete eh;
		eh = NULL;
	}

	if (scanres) {
		delete scanres;
		scanres = NULL;
	}

	if (peers) {
		peers->close();
		delete peers;
		peers = NULL;
	}

	if (udr) {
		udr->close();
		delete udr;
		udr = NULL;
	}

	if (tray_icon && !ackTrayIcon) {
		/* give user a visual hint that the tray icon exists */
		if (QSystemTrayIcon::supportsMessages()) {
			hide();
			showTrayMessage(QSystemTrayIcon::Information, 3,
					qAppName() +
					tr(" will keep running in "
					   "the system tray."));
		} else {
			QMessageBox::information(this, qAppName() +
						 tr(" systray"),
						 tr("The program will keep "
						    "running in the system "
						    "tray."));
		}
		ackTrayIcon = true;
	}

	event->accept();
}


void Wifi::wpsDialog()
{
	wpaguiTab->setCurrentWidget(wpsTab);
}


void Wifi::peersDialog()
{
	if (peers) {
		peers->close();
		delete peers;
	}

	peers = new Peers();
	if (peers == NULL)
		return;
	peers->show();
	peers->exec();
}


void Wifi::tabChanged(int index)
{
	if (index != 2)
		return;

	if (wpsRunning)
		return;

	wpsApPinEdit->setEnabled(!bssFromScan.isEmpty());
	if (bssFromScan.isEmpty())
		wpsApPinButton->setEnabled(false);
}


void Wifi::wpsPbc()
{
	char reply[20];
	size_t reply_len = sizeof(reply);

	if (ctrlRequest("WPS_PBC", reply, &reply_len) < 0)
		return;

	//wpsPinEdit->setEnabled(false);
//	if (//wpsStatusText->text().compare(tr("WPS AP in active PBC mode found"))) {
//		wpsInstructions->setText(tr("Press the push button on the AP to "
//					 "start the PBC mode."));
//	} else {
//		wpsInstructions->setText(tr("If you have not yet done so, press "
//					 "the push button on the AP to start "
//					 "the PBC mode."));
//	}
	//wpsStatusText->setText(tr("Waiting for Registrar"));
	wpsRunning = true;
}


void Wifi::wpsGeneratePin()
{
	char reply[20];
	size_t reply_len = sizeof(reply) - 1;

	if (ctrlRequest("WPS_PIN any", reply, &reply_len) < 0)
		return;

	reply[reply_len] = '\0';

	//wpsPinEdit->setText(reply);
	//wpsPinEdit->setEnabled(true);
	wpsInstructions->setText(tr("Enter the generated PIN into the Registrar "
				 "(either the internal one in the AP or an "
				 "external one)."));
	//wpsStatusText->setText(tr("Waiting for Registrar"));
	wpsRunning = true;
}


void Wifi::setBssFromScan(const QString &bssid)
{
	bssFromScan = bssid;
	wpsApPinEdit->setEnabled(!bssFromScan.isEmpty());
	wpsApPinButton->setEnabled(wpsApPinEdit->text().length() == 8);
	//wpsStatusText->setText(tr("WPS AP selected from scan results"));
	wpsInstructions->setText(tr("If you want to use an AP device PIN, e.g., "
				 "from a label in the device, enter the eight "
				 "digit AP PIN and click Use AP PIN button."));
}


void Wifi::wpsApPinChanged(const QString &text)
{
	wpsApPinButton->setEnabled(text.length() == 8);
}


void Wifi::wpsApPin()
{
	char reply[20];
	size_t reply_len = sizeof(reply);

	QString cmd("WPS_REG " + bssFromScan + " " + wpsApPinEdit->text());
	if (ctrlRequest(cmd.toLocal8Bit().constData(), reply, &reply_len) < 0)
		return;

	//wpsStatusText->setText(tr("Waiting for AP/Enrollee"));
	wpsRunning = true;
}


void Wifi::stopWpsRun(bool success)
{
	if (wpsRunning)
		//wpsStatusText->setText(success ? tr("Connected to the network") :
	//			       tr("Stopped"));
	else
		//wpsStatusText->setText("");
	//wpsPinEdit->setEnabled(false);
	wpsInstructions->setText("");
	wpsRunning = false;
	bssFromScan = "";
	wpsApPinEdit->setEnabled(false);
	wpsApPinButton->setEnabled(false);
}
#endif

#ifdef CONFIG_NATIVE_WINDOWS

#ifndef WPASVC_NAME
#define WPASVC_NAME TEXT("wpasvc")
#endif

class ErrorMsg : public QMessageBox {
public:
	ErrorMsg(QWidget *parent, DWORD last_err = GetLastError());
	void showMsg(QString msg);
private:
	DWORD err;
};

ErrorMsg::ErrorMsg(QWidget *parent, DWORD last_err) :
	QMessageBox(parent), err(last_err)
{
	setWindowTitle(tr("wpa_gui error"));
	setIcon(QMessageBox::Warning);
}

void ErrorMsg::showMsg(QString msg)
{
	LPTSTR buf;

	setText(msg);
	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER |
			  FORMAT_MESSAGE_FROM_SYSTEM,
			  NULL, err, 0, (LPTSTR) (void *) &buf,
			  0, NULL) > 0) {
		QString msg = QString::fromWCharArray(buf);
		setInformativeText(QString("[%1] %2").arg(err).arg(msg));
		LocalFree(buf);
	} else {
		setInformativeText(QString("[%1]").arg(err));
	}

	exec();
}


void Wifi::startService()
{
	SC_HANDLE svc, scm;

	scm = OpenSCManager(0, 0, SC_MANAGER_CONNECT);
	if (!scm) {
		ErrorMsg(this).showMsg(tr("OpenSCManager failed"));
		return;
	}

	svc = OpenService(scm, WPASVC_NAME, SERVICE_START);
	if (!svc) {
		ErrorMsg(this).showMsg(tr("OpenService failed"));
		CloseServiceHandle(scm);
		return;
	}

	if (!StartService(svc, 0, NULL)) {
		ErrorMsg(this).showMsg(tr("Failed to start wpa_supplicant "
				       "service"));
	}

	CloseServiceHandle(svc);
	CloseServiceHandle(scm);
}


void Wifi::stopService()
{
	SC_HANDLE svc, scm;
	SERVICE_STATUS status;

	scm = OpenSCManager(0, 0, SC_MANAGER_CONNECT);
	if (!scm) {
		ErrorMsg(this).showMsg(tr("OpenSCManager failed"));
		return;
	}

	svc = OpenService(scm, WPASVC_NAME, SERVICE_STOP);
	if (!svc) {
		ErrorMsg(this).showMsg(tr("OpenService failed"));
		CloseServiceHandle(scm);
		return;
	}

	if (!ControlService(svc, SERVICE_CONTROL_STOP, &status)) {
		ErrorMsg(this).showMsg(tr("Failed to stop wpa_supplicant "
				       "service"));
	}

	CloseServiceHandle(svc);
	CloseServiceHandle(scm);
}


bool Wifi::serviceRunning()
{
	SC_HANDLE svc, scm;
	SERVICE_STATUS status;
	bool running = false;

	scm = OpenSCManager(0, 0, SC_MANAGER_CONNECT);
	if (!scm) {
		debug("OpenSCManager failed: %d", (int) GetLastError());
		return false;
	}

	svc = OpenService(scm, WPASVC_NAME, SERVICE_QUERY_STATUS);
	if (!svc) {
		debug("OpenService failed: %d", (int) GetLastError());
		CloseServiceHandle(scm);
		return false;
	}

	if (QueryServiceStatus(svc, &status)) {
		if (status.dwCurrentState != SERVICE_STOPPED)
			running = true;
	}

	CloseServiceHandle(svc);
	CloseServiceHandle(scm);

	return running;
}
#endif // CONFIG_NATIVE_WINDOWS 
void Wifi::addInterface()
{
    #if 0
	if (add_iface) {
		add_iface->close();
		delete add_iface;
	}
	add_iface = new AddInterface(this, this);
	add_iface->show();
	add_iface->exec();
    #endif
}

