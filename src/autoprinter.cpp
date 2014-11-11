#include "autoprinter.h"
#include <QtGui>
#include <windows.h>

QReadWriteLock PndinProcFilesLock;
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
DirectoryMonitor::DirectoryMonitor( const QString &strDir )
	: m_strDir(strDir)
	, m_bMonitor(true)
{
}

void DirectoryMonitor::run()
{
	const int buf_size = 1024;  
	TCHAR buf[buf_size];  

	DWORD dwBufWrittenSize;  
	HANDLE hDir;  

	hDir = CreateFile(m_strDir.toStdString().c_str(), FILE_LIST_DIRECTORY, 
		FILE_SHARE_WRITE | FILE_SHARE_READ, NULL, OPEN_EXISTING,   
		FILE_FLAG_BACKUP_SEMANTICS, NULL);   

	if (hDir == INVALID_HANDLE_VALUE)  
	{  
		DWORD dwErrorCode;  
		dwErrorCode = GetLastError();  
		CloseHandle(hDir);   
		return;
	}  

	while(m_bMonitor)  
	{  
		if(ReadDirectoryChangesW(hDir, &buf, buf_size, FALSE, 
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

	::CloseHandle(hDir);  
}

DirectoryMonitor::~DirectoryMonitor()
{
	m_bMonitor = false;
}

void DirectoryMonitor::Stop()
{
	m_bMonitor = false;
}

//====================================================================================
AutoPrinter::AutoPrinter(QWidget *parent, Qt::WFlags flags)
	: QMainWindow(parent, flags)
	, m_FgImgPos(0, 0)
	, m_FgMaskSize(800, 600)
	, m_pMaskReviewWidget(NULL)
	, m_pThreadMonitor(NULL)
{
	ui.setupUi(this);
	ui.ImgHorzPosSlider->setRange(0, 100);
	ui.ImgHorzPosSlider->setValue(0);
	ui.ImgHorzPosSlider->setTickInterval(1);

	ui.btnStop->setEnabled(false);

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

	// Mask width(height), Mask Coordinate.
	connect(ui.spBoxMaskHeight, SIGNAL(valueChanged(int)), this, SLOT(OnMaskHeightChange(int)));
	connect(ui.spBoxMaskWeight, SIGNAL(valueChanged(int)), this, SLOT(OnMaskWidthChange(int)));
}

AutoPrinter::~AutoPrinter()
{
	if (m_pThreadMonitor)
	{
		m_pThreadMonitor->terminate();
		delete m_pThreadMonitor;
	}
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
}

void AutoPrinter::OnSelectBackupDir()
{
	QFileDialog fileDialog(this);
	if (!m_strBackupDir.isEmpty())
		fileDialog.setDirectory(m_strBackupDir);
	m_strBackupDir = fileDialog.getExistingDirectory();

	ui.lnEdtBackupDir->setText(m_strBackupDir);
}

void AutoPrinter::OnSelectOutputDir()
{
	QFileDialog fileDialog(this);
	if (!m_strOutputDir.isEmpty())
		fileDialog.setDirectory(m_strOutputDir);
	m_strOutputDir = fileDialog.getExistingDirectory();

	ui.lnEdtOutputDir->setText(m_strOutputDir);
}

void AutoPrinter::OnSelectTemplatePath()
{
	QFileDialog fileDialog(this);
	if (!m_strTemplateFile.isEmpty())
		fileDialog.setDirectory(m_strTemplateFile);
	m_strTemplateFile = fileDialog.getOpenFileName();

	ui.lnEdtTemplate->setText(m_strTemplateFile);
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
		m_pThreadMonitor = new DirectoryMonitor(m_strScanDir);
		connect(m_pThreadMonitor, SIGNAL(directoryChange()), this, SLOT(OnMonitorDirChange()));
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

void AutoPrinter::InitTemplatePreview()
{
	if (m_pMaskReviewWidget == NULL)
		m_pMaskReviewWidget = new CombineImageMaskReview(m_strTemplateFile);

	QSize size = m_pMaskReviewWidget->GetTemplateImageSize();
	int nMaxWidth = size.width() - ui.spBoxMaskWeight->value();
	int nMaxHeight = size.height() - ui.spBoxMaskHeight->value();
	ui.ImgHorzPosSlider->setRange(0, nMaxWidth);
	ui.ImgVertPosSlider->setRange(0, nMaxHeight);
}

void AutoPrinter::OnMaskHeightChange( int val )
{
	if (m_strTemplateFile.isEmpty())
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

	QFileInfoList fileInfoList = scanDir.entryInfoList();

	QFileInfoList::iterator it;
	for (it = fileInfoList.begin(); it != fileInfoList.end(); ++it)
	{
		QFile file(it->absoluteFilePath());
		QString newPath = m_strBackupDir + "\\" + it->fileName();
		QFile::rename(it->absoluteFilePath(), newPath);

		QReadLocker lock(&PndinProcFilesLock);
		m_listPndinProcFiles.append(newPath);
	}

}
