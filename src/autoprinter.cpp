#include "autoprinter.h"
#include <QtGui>

//====================================================================================
#define SETTING_TEMPLATEFILE	"TemplateFile"
#define SETTING_SCANPATH		"ScanPath"
#define SETTING_OUTPUTPATH		"OutputPath"
#define SETTING_BACKUPPATH		"BackupPath"

#define TAB_INDEX_MAIN		0
#define TAB_INDEX_FRAME		1
#define TAB_INDEX_OUTPUT	2
#define TAB_INDEX_PRINTER	3

QStringList g_listPndinProcFiles;
QReadWriteLock g_pndinProcFilesLock;

QStringList g_listPndinPrintFiles;
QReadWriteLock g_pndinPrintFilesLock;

QWaitCondition g_conFileArrived;
QMutex g_mtxFileArrived;

QWaitCondition g_conPndinPrint;
QMutex g_mtxPndinPrint;

//====================================================================================
CombineImageMaskReview::CombineImageMaskReview( const QString &strImage )
	: QWidget()
	, m_imgTemplate(strImage)
	, m_nPosX(0)
	, m_nPosY(0)
{
}

QSize CombineImageMaskReview::GetTemplateImageSize() const
{
	return m_imgTemplate.size();
}

void CombineImageMaskReview::SetMaskPosX( int posX )
{
	m_nPosX = posX;
}

void CombineImageMaskReview::SetMaskPosY( int posY )
{
	m_nPosY = posY;
}

void CombineImageMaskReview::SetMaskSize( const QSize &size )
{
	m_sizeMask.setWidth(size.width());
	m_sizeMask.setHeight(size.height());
}

void CombineImageMaskReview::paintEvent( QPaintEvent *event )
{
	QImage imgPreview(m_imgTemplate.size(), QImage::Format_ARGB32_Premultiplied);

	QPainter painterPre(&imgPreview);
	QBrush whiteBrush(Qt::white);
	painterPre.drawImage(QPoint(0, 0), m_imgTemplate);
	painterPre.fillRect(m_nPosX, m_nPosY, m_sizeMask.width(), m_sizeMask.height(), whiteBrush);
	painterPre.end();

	QPainter painter(this);
	painter.drawImage(QPoint(0,0), imgPreview);
	painter.end();

	QWidget::paintEvent(event);
}

//====================================================================================
DirectoryMonitorThread::DirectoryMonitorThread( const QString &strDir )
	: m_strMonDir(strDir)
	, m_bRun(true)
{
}

void DirectoryMonitorThread::run()
{
	const int buf_size = 1024;  
	TCHAR buf[buf_size];  

	DWORD dwBufWrittenSize;  

	m_hMonDir = CreateFile(m_strMonDir.toStdString().c_str(), FILE_LIST_DIRECTORY, 
		FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_EXISTING,   
		FILE_FLAG_BACKUP_SEMANTICS, NULL);   

	if (m_hMonDir == INVALID_HANDLE_VALUE)  
	{  
		DWORD dwErrorCode;  
		dwErrorCode = GetLastError();  
		return;
	}  

	while(m_bRun)  
	{  
		if(ReadDirectoryChangesW(m_hMonDir, &buf, buf_size, FALSE, 
			FILE_NOTIFY_CHANGE_FILE_NAME   
			, &dwBufWrittenSize, NULL, NULL))  
		{  
			FILE_NOTIFY_INFORMATION * pfiNotifyInfo = (FILE_NOTIFY_INFORMATION*)buf;  
			DWORD dwNextEntryOffset;  

			do  
			{  
				dwNextEntryOffset = pfiNotifyInfo->NextEntryOffset;  
				DWORD dwAction = pfiNotifyInfo->Action;   
				DWORD dwFileNameLength = pfiNotifyInfo->FileNameLength;  

				if (dwAction == FILE_ACTION_ADDED)
				{
					emit directoryChange();
				}

				if(dwNextEntryOffset != 0)  
				{  
					pfiNotifyInfo= (FILE_NOTIFY_INFORMATION*)((BYTE*)pfiNotifyInfo + dwNextEntryOffset);  
				}  

			}while (dwNextEntryOffset != 0);  
		}  
	}  
}

DirectoryMonitorThread::~DirectoryMonitorThread()
{
	::CloseHandle(m_hMonDir);
	m_hMonDir = NULL;
}

void DirectoryMonitorThread::Stop()
{
	m_bRun = false;
}

//====================================================================================
PhotoCombineThread::PhotoCombineThread(PhotoCombiner* pPhotoCombiner)
	: m_bRun(true)
	, m_pPhotoCombiner(pPhotoCombiner)
{

}

void PhotoCombineThread::run()
{
	while (m_bRun && m_pPhotoCombiner)
	{
		{
			QReadLocker locker(&g_pndinProcFilesLock);
			if (g_listPndinProcFiles.isEmpty())
			{
				g_conFileArrived.wait(&g_mtxFileArrived);
				continue;
			}
		}
		
		{
			QWriteLocker locker(&g_pndinProcFilesLock);
			m_pPhotoCombiner->CombineImage(g_listPndinProcFiles.first());
			g_listPndinProcFiles.removeFirst();
		}
	}
}

//====================================================================================
PrinterThread::PrinterThread( PhotoPrinter* pPhotoPrinter )
	: m_bRun(true)
	, m_pPhotoPrinter(pPhotoPrinter)
{

}

void PrinterThread::run()
{
	while (m_bRun && m_pPhotoPrinter)
	{
		{
			QReadLocker locker(&g_pndinProcFilesLock);
			if (g_listPndinProcFiles.isEmpty())
			{
				g_conFileArrived.wait(&g_mtxFileArrived);
				continue;
			}
		}

		{
			QWriteLocker lock(&g_pndinPrintFilesLock);
			m_pPhotoPrinter->PrintImage(g_listPndinPrintFiles.first());
			g_listPndinPrintFiles.removeFirst();
		}
	}
}


//====================================================================================
OuputDisplayWidget::OuputDisplayWidget( QWidget *parent /*= 0*/ )
	: QWidget(parent)
{
	 setAutoFillBackground(true);
}

void OuputDisplayWidget::SetDisplayImage( const QImage &image )
{
	
}

void OuputDisplayWidget::paintEvent( QPaintEvent *event )
{
	QWidget::paintEvent(event);
}


//====================================================================================
AutoPrinter::AutoPrinter(QWidget *parent, Qt::WFlags flags)
	: QMainWindow(parent, flags)
	, m_FgImgPos(0, 0)
	, m_FgMaskSize(800, 600)
	, m_pMaskReviewWidget(NULL)
	, m_pThreadMonitor(NULL)
	, m_pThreadCombine(NULL)
	, m_pThreadPrint(NULL)
{
	QString strSetting = QApplication::applicationDirPath() + "/setting.ini";;
	m_pSettings = new QSettings(strSetting, QSettings::IniFormat, this);

	m_strTmptFilePath = m_pSettings->value(SETTING_TEMPLATEFILE).toString();
	m_strScanDir = m_pSettings->value(SETTING_SCANPATH).toString();
	m_strBackupDir = m_pSettings->value(SETTING_BACKUPPATH).toString();
	m_strOutputDir = m_pSettings->value(SETTING_OUTPUTPATH).toString();

	ui.setupUi(this);

	QImage image(m_strTmptFilePath);
	QPalette pal(ui.widgetOutputDisplay->palette());
	pal.setBrush(backgroundRole(), QBrush(image.scaled(ui.widgetOutputDisplay->size(), Qt::KeepAspectRatio, 
		Qt::SmoothTransformation)));
	ui.widgetOutputDisplay->setPalette(pal);

	ui.lnEdtTemplate->setText(m_strTmptFilePath);
	ui.lnEdtScanDir->setText(m_strScanDir);
	ui.lnEdtBackupDir->setText(m_strBackupDir);
	ui.lnEdtOutputDir->setText(m_strOutputDir);

	ui.ImgHorzPosSlider->setRange(0, 100);
	ui.ImgHorzPosSlider->setValue(0);
	ui.ImgHorzPosSlider->setTickInterval(1);

	ui.btnStop->setEnabled(false);

	ui.spBoxCopyNum->setRange(1, 99);
	ui.spBoxCopyNum->setValue(1);

	ui.labCompletedCount->setText("0");
	ui.labOutputReview->setText("");

	// Kick start the print thread first.
	m_pThreadPrint = new PrinterThread(this);
	m_pThreadPrint->start();

	// Initial printers combobox.
	QPrinterInfo PrinterInfo;
	QList<QPrinterInfo> printerList = PrinterInfo.availablePrinters();
	for (int i = 0; i < printerList.size(); ++i)
	{
		PrinterInfo = printerList.at(i);
		QString printerName = PrinterInfo.printerName();
		ui.comboBoxPrinters->addItem(printerName);
		if (PrinterInfo.isDefault())
			ui.comboBoxPrinters->setCurrentIndex(i);
	}

	connect(ui.ImgHorzPosSlider, SIGNAL(valueChanged(int)), this, SLOT(OnPosHorizontalChange()));
	connect(ui.ImgVertPosSlider, SIGNAL(valueChanged(int)), this, SLOT(OnPosVerticalChange()));

	// Template File, Backup Dir, Output Dir, Scan Dir Path Select buttons.
	connect(ui.btnSelectScanPath, SIGNAL(pressed()), this, SLOT(OnSelectScanDir()));
	connect(ui.btnSelectBackupPath, SIGNAL(pressed()), this, SLOT(OnSelectBackupDir()));
	connect(ui.btnSelectOutputPath, SIGNAL(pressed()), this, SLOT(OnSelectOutputDir()));
	connect(ui.btnSelectTemplate, SIGNAL(pressed()), this, SLOT(OnSelectTemplatePath()));

	// Start, Stop, Save Setting function buttons.
	connect(ui.btnStart, SIGNAL(pressed()), this, SLOT(OnMonitorFolderStart()));
	connect(ui.btnStop, SIGNAL(pressed()), this, SLOT(OnMonitorFolderStop()));
	connect(ui.btnSaveSetting, SIGNAL(pressed()), this, SLOT(OnSaveSettings()));
	connect(ui.btnPrint, SIGNAL(pressed()), this, SLOT(OnPrintCopy()));

	// Mask width(height), Mask Coordinate.
	connect(ui.spBoxMaskHeight, SIGNAL(valueChanged(int)), this, SLOT(OnMaskHeightChange(int)));
	connect(ui.spBoxMaskWeight, SIGNAL(valueChanged(int)), this, SLOT(OnMaskWidthChange(int)));

	// Tab change.
	connect(ui.tabWidget, SIGNAL(currentChanged(int)), this, SLOT(OnCurrentChanged(int)));

	// Ouput tab's widgets.
	connect(ui.lstViewOutput, SIGNAL(clicked(const QModelIndex &)), this, SLOT(OnOuputItemSelected(const QModelIndex &)));
	connect(ui.lnEdtFileName, SIGNAL(textChanged(const QString &)), this, SLOT(OnFindOutputFileName(const QString &)));
	connect(ui.btnRefreshOuputList, SIGNAL(pressed()), this, SLOT(OnUpdateOutputList()));
}

AutoPrinter::~AutoPrinter()
{
	if (m_pThreadMonitor)
	{
		m_pThreadMonitor->terminate();
		delete m_pThreadMonitor;
	}

	if (m_pThreadCombine)
	{
		m_pThreadCombine->terminate();
		delete m_pThreadCombine;
	}

	if (m_pThreadPrint)
	{
		m_pThreadPrint->terminate();
		delete m_pThreadPrint;
	}
	

	//m_pSettings->sync();
}

void AutoPrinter::paintEvent(QPaintEvent *event)
{
	QImage imgTemplate("K:\\test\\template.jpg");
	QImage imgInput("K:\\test\\input.png");
	QImage imgOutput(imgTemplate.size(), QImage::Format_ARGB32_Premultiplied);
	QImage imgMask(m_FgMaskSize, QImage::Format_ARGB32_Premultiplied);

	QBrush whiteBrush(Qt::white);
	QPainter maskPainter(&imgMask);
	maskPainter.setBackground(whiteBrush);

	QPainter outputPainter(&imgOutput);
	outputPainter.drawImage(QPoint(0, 0), imgTemplate);
	outputPainter.drawImage(m_FgImgPos, imgMask);
	outputPainter.end();

	QPainter painter(this);
	painter.drawImage(QPoint(0, 0), imgOutput);
	painter.end();
	QMainWindow::paintEvent(event);	
}

void AutoPrinter::resizeEvent( QResizeEvent *event )
{
	//QImage image(m_strTmptFilePath);
	//QPalette pal(ui.widgetOutputDisplay->palette());
	//pal.setBrush(backgroundRole(), QBrush(image.scaled(ui.widgetOutputDisplay->size(), Qt::KeepAspectRatio, 
	//	Qt::FastTransformation)));
	//ui.widgetOutputDisplay->setPalette(pal);

	QMainWindow::resizeEvent(event);
}

void AutoPrinter::OnPosHorizontalChange()
{
	m_FgImgPos.setX(ui.ImgHorzPosSlider->value());
	update();
}

void AutoPrinter::OnPosVerticalChange()
{
	m_FgImgPos.setY(ui.ImgVertPosSlider->value());
	update();
}

void AutoPrinter::OnSelectScanDir()
{
	QFileDialog fileDialog(this);
	if (!m_strScanDir.isEmpty())
		fileDialog.setDirectory(m_strScanDir);
	m_strScanDir = fileDialog.getExistingDirectory();

	ui.lnEdtScanDir->setText(m_strScanDir);
	m_pSettings->setValue(SETTING_SCANPATH, m_strScanDir);
}

void AutoPrinter::OnSelectBackupDir()
{
	QFileDialog fileDialog(this);
	if (!m_strBackupDir.isEmpty())
		fileDialog.setDirectory(m_strBackupDir);
	m_strBackupDir = fileDialog.getExistingDirectory();

	ui.lnEdtBackupDir->setText(m_strBackupDir);
	m_pSettings->setValue(SETTING_BACKUPPATH, m_strBackupDir);
}

void AutoPrinter::OnSelectOutputDir()
{
	QFileDialog fileDialog(this);
	if (!m_strOutputDir.isEmpty())
		fileDialog.setDirectory(m_strOutputDir);
	m_strOutputDir = fileDialog.getExistingDirectory();

	ui.lnEdtOutputDir->setText(m_strOutputDir);
	m_pSettings->setValue(SETTING_OUTPUTPATH, m_strOutputDir);
}

void AutoPrinter::OnSelectTemplatePath()
{
	QFileDialog fileDialog(this);
	if (!m_strTmptFilePath.isEmpty())
		fileDialog.setDirectory(m_strTmptFilePath);
	m_strTmptFilePath = fileDialog.getOpenFileName();

	ui.lnEdtTemplate->setText(m_strTmptFilePath);
	m_pSettings->setValue(SETTING_TEMPLATEFILE, m_strTmptFilePath);
}

void AutoPrinter::OnMonitorFolderStart()
{
	if (m_strScanDir.isEmpty() || !QFile::exists(m_strScanDir))
	{
		QMessageBox::warning(this, AutoPrinter::tr("Auto Printer"), AutoPrinter::tr("Please check your setting of scan path"));
		return;
	}

	if (m_strBackupDir.isEmpty() || !QFile::exists(m_strBackupDir))
	{
		QMessageBox::warning(this, AutoPrinter::tr("Auto Printer"), AutoPrinter::tr("Please check your setting of backup path"));
		return;
	}

	if (m_pThreadMonitor && m_pThreadMonitor->isRunning())
	{
		m_pThreadMonitor->terminate();
		delete m_pThreadMonitor;
		m_pThreadMonitor = NULL;
	}

	if (m_pThreadMonitor == NULL)
	{
		m_pThreadMonitor = new DirectoryMonitorThread(m_strScanDir);
		connect(m_pThreadMonitor, SIGNAL(directoryChange()), this, SLOT(OnMonitorDirChange()));
	}

	if (m_pThreadCombine == NULL)
	{
		m_pThreadCombine = new PhotoCombineThread(this);
		m_pThreadCombine->start();
	}

	m_pThreadMonitor->start();

	ui.btnStart->setEnabled(false);
	ui.btnStop->setEnabled(true);
}

void AutoPrinter::OnMonitorFolderStop()
{
	if (m_pThreadMonitor)
		m_pThreadMonitor->Stop();

	ui.btnStart->setEnabled(true);
	ui.btnStop->setEnabled(false);
}

void AutoPrinter::OnSaveSettings()
{
	// TODO:
}

void AutoPrinter::InitTemplatePreviewTab()
{
	if (m_pMaskReviewWidget == NULL)
		m_pMaskReviewWidget = new CombineImageMaskReview(m_strTmptFilePath);

	QSize size = m_pMaskReviewWidget->GetTemplateImageSize();
	int nMaxWidth = size.width() - ui.spBoxMaskWeight->value();
	int nMaxHeight = size.height() - ui.spBoxMaskHeight->value();
	ui.ImgHorzPosSlider->setRange(0, nMaxWidth);
	ui.ImgVertPosSlider->setRange(0, nMaxHeight);
}

void AutoPrinter::OnMaskHeightChange( int val )
{
	if (m_strTmptFilePath.isEmpty())
	{
		QMessageBox::warning(this, AutoPrinter::tr("Auto Printer"), AutoPrinter::tr("Please select a template file first!"));
		return;
	}
}

void AutoPrinter::OnMaskWidthChange( int val )
{

}

void AutoPrinter::OnMaskCoordChangeX()
{

}

void AutoPrinter::OnMaskCoordChangeY()
{

}

void AutoPrinter::OnMonitorDirChange()
{
	QDir scanDir(m_strScanDir);
	if (scanDir.count() == 0)
		return;

	QFileInfoList fileInfoList = scanDir.entryInfoList(
		QDir::Files|QDir::NoSymLinks|QDir::NoDotAndDotDot
		|QDir::NoDot|QDir::NoDotDot);

	QFileInfoList::iterator it;
	for (it = fileInfoList.begin(); it != fileInfoList.end(); ++it)
	{
		QFile file(it->absoluteFilePath());
		QString newPath = m_strBackupDir + "\\" + it->fileName();
		QFile::rename(it->absoluteFilePath(), newPath);

		QWriteLocker lock(&g_pndinProcFilesLock);
		g_listPndinProcFiles.push_back(it->fileName());
		g_listPndinProcFiles.removeDuplicates();

		g_conFileArrived.wakeAll();
	}

}

void AutoPrinter::CombineImage( const QString &strInputImage )
{
	if (!QFile::exists(strInputImage))
		return;

	if (!QFile::exists(m_strTmptFilePath))
	{
		QMessageBox::warning(this, AutoPrinter::tr("Auto Printer"), AutoPrinter::tr("Please check your setting of template image path"));
		return;
	}

	QString strImgBackup = m_strBackupDir + "\\" + strInputImage;
	QString strImgOutput = m_strOutputDir + "\\" + strInputImage;
	QImage imgTemplate(m_strTmptFilePath);
	QImage imgInput(strImgBackup);
	QImage imgOutput(imgTemplate.size(), QImage::Format_ARGB32_Premultiplied);

	QPainter outputPainter(&imgOutput);
	outputPainter.drawImage(QPoint(0, 0), imgTemplate);
	outputPainter.drawImage(QPoint(0, 0), imgInput);
	outputPainter.end();

	imgOutput.save(strImgOutput);

	AddPendingPrintImage(strImgOutput);
}

void AutoPrinter::PrintImage( const QString &strImagePath )
{

}

void AutoPrinter::InitPrinterSelectTab()
{

}

void AutoPrinter::OnCurrentChanged(int nIndex)
{
	switch(nIndex)
	{
	case TAB_INDEX_PRINTER:
		{
			InitPrinterSelectTab();
		}
		break;
	case TAB_INDEX_OUTPUT:
		{
			InitCombineOutputTab();
		}
		break;
	default:
		break;
	}
}

void AutoPrinter::AddPendingPrintImage(const QString &strImagePath, int nTimes, bool bPreempt)
{
	QWriteLocker lock(&g_pndinPrintFilesLock);
	int i = 0;
	do 
	{
		if (bPreempt)
			g_listPndinPrintFiles.push_front(strImagePath);
		else
			g_listPndinPrintFiles.push_back(strImagePath);

	} while (++i < nTimes);

	g_conPndinPrint.wakeAll();
}

void AutoPrinter::OnPrintCopy()
{
	if (ui.labOutputReview->text().isEmpty())
	{
		QMessageBox::warning(this, AutoPrinter::tr("Auto Printer"), AutoPrinter::tr("Please select a output image first!"));
		return;
	}

	QString filePath = m_strOutputDir + "\\" + ui.labOutputReview->text();
	if (!QFile::exists(filePath))
	{
		QMessageBox::warning(this, AutoPrinter::tr("Auto Printer"), AutoPrinter::tr("Select image not exist!"));
		return;
	}
	
	AddPendingPrintImage(filePath, ui.spBoxCopyNum->value(), true);	
}

void AutoPrinter::OnUpdateOutputList()
{
	if (!QFile::exists(m_strOutputDir))
		return;

	QDir outputDir(m_strOutputDir);
	if (outputDir.count() == 0)
		return;

	QFileInfoList fileInfoList = outputDir.entryInfoList(
		QDir::Files|QDir::NoSymLinks|QDir::NoDotAndDotDot
		|QDir::NoDot|QDir::NoDotDot);

	QFileInfoList::iterator it;
	QStandardItemModel* model = new QStandardItemModel(ui.lstViewOutput);
	int i = 0;
	for (it = fileInfoList.begin(); it != fileInfoList.end(); ++it)
	{
		QStandardItem* item = new QStandardItem(it->fileName());
		item->setEditable(false);
		model->setItem(i, item);
		++i;
	}
	ui.lstViewOutput->setModel(model);
}

void AutoPrinter::InitCombineOutputTab()
{
	OnUpdateOutputList();
}

void AutoPrinter::OnOuputItemSelected( const QModelIndex &index )
{
	QString strSelectedImg = index.model()->data(index, Qt::DisplayRole).toString();
	ui.labOutputReview->setText(strSelectedImg);
}

void AutoPrinter::OnFindOutputFileName( const QString& string )
{
	ui.lstViewOutput->keyboardSearch(string);
}
