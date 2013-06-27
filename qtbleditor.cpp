#include "qtbleditor.h"
#include "gotorowdialog.h"
#include "tablepanelwidget.h"
#include "findreplacedialog.h"

#include <QMainWindow>
#include <QCloseEvent>
#include <QLabel>
#include <QSplitter>
#include <QMessageBox>
#include <QFileDialog>
#include <QSpinBox>
#include <QLabel>
#include <QTextEdit>
#include <QInputDialog>
#include <QScrollBar>
#include <QClipboard>
#include <QListWidget>

#include <QTextStream>
#include <QSettings>
#include <QFileInfo>
#include <QTextStream>
#include <QMimeData>


static const QString newTblFileName("!newstring!.tbl"), customColorsFileName("customcolors.ini"), releaseDate("23.01.2012");
const int maxRecentFiles = 10;


// global auxiliary functions

#define CONNECT_ACTION_TO_SLOT(action, slot) connect(action, SIGNAL(triggered()), slot)

QDataStream &operator <<(QDataStream &out, const TblHeader &th)
{
	return out << th.CRC << th.NodesNumber << th.HashTableSize << th.Version << th.DataStartOffset << th.HashMaxTries
		<< th.FileSize;
}

QDataStream &operator <<(QDataStream &out, const TblHashNode &tn)
{
	return out << tn.Active << tn.Index << tn.HashValue << tn.StringKeyOffset << tn.StringValOffset << tn.StringValLength;
}

// end of global auxiliary functions


extern QList<QChar> colorCodes;
extern QStringList colorStrings;
extern QList<QColor> colors;
extern QString colorHeader;
extern int colorsNum;

QTblEditor::QTblEditor(QWidget *parent, Qt::WindowFlags flags) : QMainWindow(parent, flags), _openedTables(0)
{
	ui.setupUi(this);
	ui.mainToolBar->setWindowTitle(tr("Toolbar"));

#ifdef Q_OS_MAC
	ui.actionInsertAfterCurrent->setShortcut(QKeySequence("+"));
	ui.actionAppendEntry->setShortcut(QKeySequence("Ctrl++"));
#endif
	ui.actionSwap->setShortcut(QKeySequence("Meta+Tab"));
	ui.actionChangeActive->setShortcut(QKeySequence("Tab"));

	QActionGroup *csvSeparators = new QActionGroup(this);
	csvSeparators->addAction(ui.actionCsvComma);
	csvSeparators->addAction(ui.actionCsvSemiColon);

	QList<QAction *> diffActions = QList<QAction *>() << ui.actionKeys << ui.actionStrings << ui.actionBoth << ui.actionSameStrings;
	for (int i = 0; i < diffActions.size(); ++i)
		diffActions[i]->setData(i);

	_locationLabel = new QLabel("00000 (0x00000) / 00000 (0x00000)", this);
	_locationLabel->setToolTip(tr("Current row / total rows"));
	_locationLabel->setAlignment(Qt::AlignHCenter);
	_locationLabel->setMinimumSize(_locationLabel->sizeHint());
	_locationLabel->clear();

	_keyHashLabel = new QLabel("0x00000 | 0x00000", this);
	_keyHashLabel->setToolTip(tr("Key hash value"));
	_keyHashLabel->setAlignment(Qt::AlignHCenter);
	_keyHashLabel->setMinimumSize(_keyHashLabel->sizeHint());
	_keyHashLabel->clear();

#ifndef Q_OS_MAC
	ui.statusBar->addWidget(new QLabel, 1);
#endif
	ui.statusBar->addPermanentWidget(_keyHashLabel);
	ui.statusBar->addPermanentWidget(_locationLabel);

	_leftTablePanelWidget = new TablePanelWidget(this);
	_rightTablePanelWidget = new TablePanelWidget(this);
	_leftTableWidget = _leftTablePanelWidget->tableWidget();
	_rightTableWidget = _rightTablePanelWidget->tableWidget();
	_rightTablePanelWidget->hide();

	_tableSplitter = new QSplitter(Qt::Horizontal, this);
	_tableSplitter->addWidget(_leftTablePanelWidget);
	_tableSplitter->addWidget(_rightTablePanelWidget);
	_tableSplitter->setChildrenCollapsible(false);
	setCentralWidget(_tableSplitter);

	_findReplaceDlg = new FindReplaceDialog(this);

	_startNumberingGroup = new QActionGroup(this);
	_startNumberingGroup->addAction(ui.actionStartNumberingFrom0);
	_startNumberingGroup->addAction(ui.actionStartNumberingFrom1);


	connectActions();

	connect(ui.menuView, SIGNAL(aboutToShow()), SLOT(updateToolbarStateInMenu()));

	connect(_leftTableWidget, SIGNAL(itemDoubleClicked(QTableWidgetItem *)), SLOT(editString(QTableWidgetItem *)));
	connect(_leftTableWidget, SIGNAL(currentCellChanged(int, int, int, int)), SLOT(updateLocationLabel(int)));
	connect(_leftTableWidget, SIGNAL(tableGotFocus(QWidget *)), SLOT(changeCurrentTable(QWidget *)));
	connect(_leftTableWidget, SIGNAL(itemWasDropped(QTableWidgetItem *)), SLOT(updateItem(QTableWidgetItem *)));

	connect(_rightTableWidget, SIGNAL(itemDoubleClicked(QTableWidgetItem *)), SLOT(editString(QTableWidgetItem *)));
	connect(_rightTableWidget, SIGNAL(currentCellChanged(int, int, int, int)), SLOT(updateLocationLabel(int)));
	connect(_rightTableWidget, SIGNAL(tableGotFocus(QWidget *)), SLOT(changeCurrentTable(QWidget *)));
	connect(_rightTableWidget, SIGNAL(itemWasDropped(QTableWidgetItem *)), SLOT(updateItem(QTableWidgetItem *)));

	connect(_leftTableWidget, SIGNAL(currentCellChanged(int, int, int, int)), _rightTableWidget, SLOT(changeCurrentCell(int, int)));
	connect(_rightTableWidget, SIGNAL(currentCellChanged(int, int, int, int)), _leftTableWidget, SLOT(changeCurrentCell(int, int)));
	connect(_leftTableWidget->verticalScrollBar(), SIGNAL(valueChanged(int)), _rightTableWidget->verticalScrollBar(), SLOT(setValue(int)));
	connect(_rightTableWidget->verticalScrollBar(), SIGNAL(valueChanged(int)), _leftTableWidget->verticalScrollBar(), SLOT(setValue(int)));

	connect(ui.actionShowHexInRow, SIGNAL(toggled(bool)), _leftTableWidget, SLOT(toggleDisplayHex(bool)));
	connect(ui.actionShowHexInRow, SIGNAL(toggled(bool)), _rightTableWidget, SLOT(toggleDisplayHex(bool)));
	connect(ui.actionStartNumberingFrom1, SIGNAL(toggled(bool)), _leftTableWidget, SLOT(changeRowNumberingTo1(bool)));
	connect(ui.actionStartNumberingFrom1, SIGNAL(toggled(bool)), _rightTableWidget, SLOT(changeRowNumberingTo1(bool)));

	connect(_findReplaceDlg, SIGNAL(getStrings(QString, bool, bool, bool)), SLOT(findNextString(QString, bool, bool, bool)));
	connect(this, SIGNAL(searchFinished(QList<QTableWidgetItem *>)), _findReplaceDlg, SLOT(getFoundStrings(QList<QTableWidgetItem *>)));
	connect(_findReplaceDlg, SIGNAL(currentItemChanged(QTableWidgetItem *)), SLOT(changeCurrentTableItem(QTableWidgetItem *)));
	connect(_findReplaceDlg, SIGNAL(sendingText(QTableWidgetItem *)), SLOT(recievingTextFromSingleItem(QTableWidgetItem *)));
	

	_currentTableWidget = _leftTableWidget;
	readSettings();
	updateRecentFilesActions();

	_leftTableWidget->changeRowHeaderDisplay();
	_rightTableWidget->changeRowHeaderDisplay();
}

void QTblEditor::connectActions()
{
	CONNECT_ACTION_TO_SLOT(ui.actionNew, SLOT(newTable()));
	CONNECT_ACTION_TO_SLOT(ui.actionOpen, SLOT(open()));
	CONNECT_ACTION_TO_SLOT(ui.actionReopen, SLOT(reopen()));
	CONNECT_ACTION_TO_SLOT(ui.actionSave, SLOT(save()));
	CONNECT_ACTION_TO_SLOT(ui.actionSaveAs, SLOT(saveAs()));
	CONNECT_ACTION_TO_SLOT(ui.actionSaveAll, SLOT(saveAll()));
	CONNECT_ACTION_TO_SLOT(ui.actionClose, SLOT(closeTable()));
	CONNECT_ACTION_TO_SLOT(ui.actionCloseAll, SLOT(closeAll()));

	CONNECT_ACTION_TO_SLOT(ui.actionChangeText, SLOT(changeText()));
	CONNECT_ACTION_TO_SLOT(ui.actionAppendEntry, SLOT(appendEntry()));
	CONNECT_ACTION_TO_SLOT(ui.actionInsertAfterCurrent, SLOT(insertAfterCurrent()));
	CONNECT_ACTION_TO_SLOT(ui.actionClearSelected, SLOT(deleteSelectedItems()));
	CONNECT_ACTION_TO_SLOT(ui.actionDeleteSelected, SLOT(deleteSelectedItems()));
	CONNECT_ACTION_TO_SLOT(ui.actionCopy, SLOT(copy()));
	CONNECT_ACTION_TO_SLOT(ui.actionPaste, SLOT(paste()));
	CONNECT_ACTION_TO_SLOT(ui.actionFindReplace, SLOT(showFindReplaceDialog()));
	CONNECT_ACTION_TO_SLOT(ui.actionGoTo, SLOT(goTo()));

	connect(ui.actionToolbar, SIGNAL(toggled(bool)), ui.mainToolBar, SLOT(setVisible(bool)));
	connect(ui.actionSmallRows, SIGNAL(toggled(bool)), SLOT(toggleRowsHeight(bool)));

	CONNECT_ACTION_TO_SLOT(ui.actionSupplement, SLOT(supplement()));
	CONNECT_ACTION_TO_SLOT(ui.actionSwap, SLOT(swapTables()));
	CONNECT_ACTION_TO_SLOT(ui.actionChangeActive, SLOT(activateAnotherTable()));
	CONNECT_ACTION_TO_SLOT(ui.actionKeys, SLOT(showDifferences()));
	CONNECT_ACTION_TO_SLOT(ui.actionStrings, SLOT(showDifferences()));
	CONNECT_ACTION_TO_SLOT(ui.actionBoth, SLOT(showDifferences()));
	CONNECT_ACTION_TO_SLOT(ui.actionSameStrings, SLOT(showDifferences()));

	CONNECT_ACTION_TO_SLOT(ui.actionAbout, SLOT(aboutApp()));
	connect(ui.actionAboutQt, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
}

void QTblEditor::updateRecentFilesActions()
{
	ui.menuRecentFiles->clear();
	if (!_recentFilesList.isEmpty())
	{
		ui.menuRecentFiles->setEnabled(true);
		QMutableStringListIterator i(_recentFilesList);
		while (i.hasNext())
		{
			QString filePath = i.next();
			if (QFile::exists(filePath))
				ui.menuRecentFiles->addAction(
#ifdef Q_OS_WIN32
						QDir::toNativeSeparators(filePath),
#else
						filePath,
#endif
						this, SLOT(openRecentFile()));
			else
				i.remove();
		}
	}
	else
		ui.menuRecentFiles->setEnabled(false);
}

int QTblEditor::openTableMsgBoxResult()
{
	QMessageBox openOptsDlg(this);
	openOptsDlg.setIcon(QMessageBox::Question);
	openOptsDlg.setDefaultButton(openOptsDlg.addButton(tr("Open one more"), QMessageBox::AcceptRole));
	openOptsDlg.setEscapeButton(openOptsDlg.addButton(QMessageBox::Cancel));
	openOptsDlg.addButton(tr("Replace active"), QMessageBox::NoRole);
	openOptsDlg.setText(tr("How do you want to open the table?"));
#ifdef Q_OS_MAC
	openOptsDlg.setWindowModality(Qt::WindowModal);
#endif
	return openOptsDlg.exec();
}

void QTblEditor::tableMenuSetEnabled(bool isEnabled)
{
	ui.menuTables->setEnabled(isEnabled);
	foreach (QAction *action, ui.menuTables->actions())
		action->setEnabled(isEnabled);
}

void QTblEditor::newTable()
{
	bool okPressed;
	int rowsNum = QInputDialog::getInt(this, tr("New table"), tr("Enter desired number of rows (1-100000):"), 1, 1, 100000, 1, &okPressed);
	if (okPressed)
	{
		bool areTwoTablesOpened = _openedTables == 2;
		int result = -1;
		if (_openedTables == 1)
		{
			result = openTableMsgBoxResult(); // 0 - add one more, 1 - replace active, rest - cancel
			if (result != 0 && result != 1)
				return;
		}
		if (result && !currentTablePanelWidget()->absoluteFileName().isEmpty() && !closeTable())
			return;

		if (!result || result == -1 && areTwoTablesOpened)
		{
			activateAnotherTable();
			currentTablePanelWidget()->show();
			inactiveNamedTableWidget(currentTablePanelWidget())->setActive(false);
			tableMenuSetEnabled(true);
			closeAllDialogs();
		}

		if (!_openedTables || _openedTables == 1 && (!result || result == -1))
			_openedTables++;

		_currentTableWidget->setRowCount(rowsNum);
		currentTablePanelWidget()->updateRowCountLabel();
		for (int i = 0; i < rowsNum; i++)
			_currentTableWidget->createNewEntry(i, QString::null, QString::null);
		currentTablePanelWidget()->setFilePath(newTblFileName);
		currentTablePanelWidget()->setActive(true);
		_currentTableWidget->setCurrentCell(0, 0, QItemSelectionModel::Select);

		updateWindow();
		enableTableActions(true);
		ui.actionSaveAll->setEnabled(true);
	}
}

void QTblEditor::openRecentFile()
{
	QAction *action = qobject_cast<QAction *>(sender());
#ifdef Q_OS_WIN32
	QString fileName = QDir::fromNativeSeparators(action->text());
#else
	QString fileName = action->text();
#endif
	if (loadFile(fileName))
	{
		_recentFilesList.move(_recentFilesList.indexOf(fileName), 0);
		updateRecentFilesActions();
	}
}

void QTblEditor::addToRecentFiles(const QString &fileName)
{
	int index = _recentFilesList.indexOf(fileName);
	if (index != -1) // it's already in the list
		_recentFilesList.move(index, 0);
	else
	{
		if (_recentFilesList.length() == maxRecentFiles)
			_recentFilesList.removeLast();
		_recentFilesList.prepend(fileName);
	}
	updateRecentFilesActions();
}

void QTblEditor::open()
{
	QString fileName = QFileDialog::getOpenFileName(this, tr("Open table file"), _lastPath,
		tr("All supported formats (*.tbl *.txt *.csv);;Tbl files (*.tbl);;Tab-delimited txt files (*.txt);;CSV files (*.csv);;All files (*)"));
	if (!fileName.isEmpty() && loadFile(fileName))
	{
		addToRecentFiles(fileName);
		_findReplaceDlg->needsRefind();
	}
}

void QTblEditor::reopen()
{
	if (processTable(currentTablePanelWidget()->absoluteFileName()))
	{
		updateWindow(false);
		setWindowModified(_leftTableWidget->isWindowModified() && _rightTableWidget->isWindowModified());
		updateLocationLabel(_currentTableWidget->currentRow());
	}
}

bool QTblEditor::loadFile(const QString &fileName, bool shouldShowOpenOptions)
{
	bool areTwoTablesOpened = _openedTables == 2;
	int result = shouldShowOpenOptions ? -1 : 0;
	if (_openedTables == 1 && shouldShowOpenOptions)
	{
		result = openTableMsgBoxResult(); // 0 - add one more, 1 - replace active, rest - cancel
		if (result != 0 && result != 1)
			return false;
	}

	if (result && !currentTablePanelWidget()->absoluteFileName().isEmpty() && !closeTable())
		return false;

	int row = 0, column = 0;
	if (!result || result == -1 && areTwoTablesOpened)
	{
		row = _currentTableWidget->currentRow();
		column = _currentTableWidget->currentColumn();
		activateAnotherTable();
		currentTablePanelWidget()->show();
		closeAllDialogs();
	}
	if (processTable(fileName))
	{
		if (!_openedTables || _openedTables == 1 && (!result || result == -1))
			_openedTables++;

		currentTablePanelWidget()->setFilePath(fileName);
		currentTablePanelWidget()->setActive(true);
		currentTablePanelWidget()->setWindowModified(false);
		_currentTableWidget->setCurrentCell(row, column);

		if (_openedTables == 2)
		{
			inactiveNamedTableWidget(currentTablePanelWidget())->setActive(false);
			tableMenuSetEnabled(true);
		}

		_lastPath = currentTablePanelWidget()->fileDirPath();
		enableTableActions(true);

		return true;
	}
	else
	{
		if (!result || result == -1 && areTwoTablesOpened)
		{
			currentTablePanelWidget()->hide();
			activateAnotherTable();
		}
		return false;
	}
}

bool QTblEditor::processTable(const QString &fileName)
{
	QFile inputFile(fileName);
	if (inputFile.open(QIODevice::ReadOnly))
	{
		QString extension = fileName.right(4).toLower();
		if (extension == ".tbl")
			return processTblFile(&inputFile);
		else if (extension == ".txt" || extension == ".csv")
			return processTxtOrCsvFile(&inputFile);
		else // arbitrary file opened
		{
			QTextStream in(&inputFile);
			in.readLine();
			QString secondLine = in.readLine(); // it should equal "X" if it's tbl file
			inputFile.reset();
			if (secondLine.contains('\t') || secondLine.contains(';') || secondLine.contains(',')) // text format
				return processTxtOrCsvFile(&inputFile);
			else // tbl format
				return processTblFile(&inputFile);
		}
	}
	else
	{
		QMessageBox::critical(this, QApplication::applicationName(), tr("Error opening file \"%1\"\nReason: %2")
							  .arg(fileName, inputFile.errorString()));
		return false;
	}
}

bool QTblEditor::processTblFile(QFile *inputFile)
{
	QDataStream in(inputFile);
	in.setByteOrder(QDataStream::LittleEndian);
	TblStructure tbl;
	tbl.fillHeader(in); // reading header
	DWORD numElem = tbl.header().FileSize - TblHeader::size; // number of bytes to read without header
	char *table = new char[numElem];
	if (in.readRawData(table, numElem) == numElem)
	{
		delete [] table;
		table = 0;

		inputFile->reset(); // current offset for reading = 0x00
		in.skipRawData(TblHeader::size); // current offset for reading = 0x15 (we don't need header any more)
		tbl.getStringTable(in);
		inputFile->close();

		int rowCount = tbl.header().NodesNumber;
		_currentTableWidget->setRowCount(rowCount);
		currentTablePanelWidget()->updateRowCountLabel();

		int maxKeyWidth = 0;
		for (WORD i = 0; i < rowCount; i++)
		{
			QPair<QString, QString> currentDataStrings = tbl.dataStrings(i); // reading pair <key, value>
			_currentTableWidget->createNewEntry(i, currentDataStrings.first, currentDataStrings.second);

			int currentKeyWidth = QFontMetrics(_currentTableWidget->item(i, 0)->font()).width(currentDataStrings.first);
			if (maxKeyWidth < currentKeyWidth)
				maxKeyWidth = currentKeyWidth;
		}
		_currentTableWidget->setColumnWidth(0, maxKeyWidth + 1); // making "key" column width fit all the entries

		return true;
	}
	else
	{
		delete [] table;
		table = 0;
		QMessageBox::critical(this, QApplication::applicationName(), tr("Couldn't read entire file, read only %n byte(s) after header.\n"
													"Probably file is corrupted or wrong file format.", "", numElem));
		return false;
	}
}

bool QTblEditor::processTxtOrCsvFile(QFile *inputFile)
{
	QTextStream in(inputFile);
	in.setCodec("UTF-8");
	in.setAutoDetectUnicode(true);

	QStringList entries = in.readAll().split("\n"); // TODO: maybe read line by line to improve performance
	inputFile->close();
	if (entries.isEmpty())
		return true;

	int rows = entries.size() - 1; // don't include last empty line
	_currentTableWidget->setRowCount(rows);
	currentTablePanelWidget()->updateRowCountLabel();

	QString currentLine = entries.at(0), separator = "\t", wrappingCharKey = "\"", wrappingCharValue = "\"";
	if (inputFile->fileName().right(4).toLower() == ".csv" || currentLine.contains("\",\"") || currentLine.contains("\";\""))
	{
		int secondDoubleQuoteIndex = currentLine.indexOf('\"', 1);
		if (secondDoubleQuoteIndex == -1)
		{
			QMessageBox::critical(this, QApplication::applicationName(),
								  tr("Wrong file format - all strings in *.csv should be wrapped in double quotes"));
			closeTable();
			return false;
		}
		separator = currentLine.at(secondDoubleQuoteIndex + 1);
	}
	else
	{
		QStringList s = currentLine.split(separator);
		QString currentKey = s.at(0), currentValue = s.at(1);
		if (!currentKey.startsWith(wrappingCharKey) || !currentKey.endsWith(wrappingCharKey))
			wrappingCharKey = "";
		if (!currentValue.startsWith(wrappingCharValue) || !currentValue.endsWith(wrappingCharValue))
			wrappingCharValue = "";
	}

	QString keyValueSeparator = wrappingCharKey + separator + wrappingCharValue;
	QString utf8TblEditHeaderString = QString("%1Key%1%2%3Value%3").arg(wrappingCharKey, separator, wrappingCharValue);
	QString afj666HeaderString = QString("%1String Index%1%2%3Text%3").arg(wrappingCharKey, separator, wrappingCharValue);
	int i = 0, maxKeyWidth = 0;
	if (currentLine == utf8TblEditHeaderString || currentLine == afj666HeaderString)
	{
		if (!rows)
			return true;
		currentLine = entries.at(++i);
	}
	for (; i < rows; currentLine = entries.at(++i))
	{
		if (!currentLine.contains(keyValueSeparator))
		{
			QMessageBox::critical(this, QApplication::applicationName(), tr("Wrong file format - separator is absent at line %1").arg(i + 1));
			closeTable();
			return false;
		}

		QStringList s = currentLine.split(keyValueSeparator);
		QString currentKey = s.at(0), currentValue = s.at(1);
		currentValue.replace("\\n", "\n");

		_currentTableWidget->createNewEntry(i, currentKey, currentValue);

		int currentKeyWidth = QFontMetrics(_currentTableWidget->item(i, 0)->font()).width(currentKey);
		if (maxKeyWidth < currentKeyWidth)
			maxKeyWidth = currentKeyWidth;
	}
	_currentTableWidget->setColumnWidth(0, maxKeyWidth + 1);

	return true;
}

bool QTblEditor::closeTable(bool hideTable)
{
	if (!_openedTables)
		return true;

	if (wasSaved())
	{
		_locationLabel->clear();
		_keyHashLabel->clear();
		closeAllDialogs();

		int row = _currentTableWidget->currentRow(), column = _currentTableWidget->currentColumn();
		QString filePath = currentTablePanelWidget()->absoluteFileName();
		if (hideTable || filePath == newTblFileName)
		{
			currentTablePanelWidget()->clearContents();
			_openedTables--;
		}
		if (_openedTables)
		{
			if (hideTable || filePath == newTblFileName)
				currentTablePanelWidget()->hide();
			activateAnotherTable();
			currentTablePanelWidget()->setActive(true);
			_currentTableWidget->setCurrentCell(row, column);

			tableMenuSetEnabled(false);
			updateWindow(_currentTableWidget->isWindowModified());
			ui.actionSaveAll->setEnabled(_currentTableWidget->isWindowModified());
		}
		else
		{
			enableTableActions(false);
			updateWindow(false);
			ui.actionSaveAll->setEnabled(false);
		}
		return true;
	}
	return false;
}

void QTblEditor::enableTableActions(bool state)
{
	QList<QAction *> actions = QList<QAction *>() << ui.actionSaveAs << ui.actionClose << ui.actionCloseAll << ui.menuEdit->actions();
	foreach (QAction *action, actions)
		action->setEnabled(state);
	ui.menuEdit->setEnabled(state);
}

void QTblEditor::closeEvent(QCloseEvent *event)
{
	if (closeAll(false))
	{
		writeSettings();
		event->accept();
	}
	else
		event->ignore();
}

bool QTblEditor::wasSaved()
{
	if (_currentTableWidget->isWindowModified())
	{
		QString text = tr("The table \"%1\" has been modified.").arg(currentTablePanelWidget()->fileName());
		QString informativeText = tr("Do you want to save your changes?");
#ifdef Q_OS_MAC
		QMessageBox macMsgBox(this);
		macMsgBox.setIcon(QMessageBox::Warning);
		macMsgBox.setText(text);
		macMsgBox.setInformativeText(informativeText);
		macMsgBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
		macMsgBox.setDefaultButton(QMessageBox::Save);
		macMsgBox.setWindowModality(Qt::WindowModal);
		int answer = macMsgBox.exec();
#else
		int answer = QMessageBox::warning(this, QApplication::applicationName(), QString("%1\n%2").arg(text, informativeText),
										  QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
#endif
		if (answer == QMessageBox::Save)
			save();
		else if (answer == QMessageBox::Cancel)
			return false;
	}
	return true;
}

void QTblEditor::save()
{
	QString fileName = currentTablePanelWidget()->absoluteFileName();
	if (fileName == newTblFileName)
		saveAs();
	else
		saveFile(fileName);
}

void QTblEditor::saveAs()
{
	QString fileName = currentTablePanelWidget()->absoluteFileName();
#ifdef Q_OS_WIN32
	if (QSysInfo::WindowsVersion <= QSysInfo::WV_XP) // workaround for XP and earlier Windows versions
	{
		QFileInfo info(fileName);
		fileName = info.canonicalPath() + '/' + info.baseName(); // removing extension
	}
#endif
	QString fileNameToSave = QFileDialog::getSaveFileName(this, tr("Save table"), fileName,
														  tr("Tbl files (*.tbl);;Tab-delimited text files (*.txt);;CSV files (*.csv);;All files (*)"));
	if (!fileNameToSave.isEmpty() && saveFile(fileNameToSave))
	{
		currentTablePanelWidget()->setFilePath(fileNameToSave);
		addToRecentFiles(fileNameToSave);
	}
}

void QTblEditor::saveAll()
{
	if (_currentTableWidget->isWindowModified())
		save();
	if (_openedTables == 2)
	{
		activateAnotherTable();
		if (_currentTableWidget->isWindowModified())
			save();
		activateAnotherTable();
	}
	ui.actionSaveAll->setDisabled(true);
}

bool QTblEditor::saveFile(const QString &fileName)
{
	QByteArray bytesToWrite; // first of all, write everything to buffer
	QString extension = fileName.right(4);
	bool isCsv = extension == ".csv";

	DWORD fileSize;
	if (extension == ".tbl")
		fileSize = writeAsTbl(bytesToWrite);
	else if (extension == ".txt" || isCsv)
		fileSize = writeAsText(bytesToWrite, isCsv);
	else // any file
	{
		QMessageBox msgbox(this);
		msgbox.setWindowTitle(QApplication::applicationName());
		msgbox.setText(tr("Choose file format"));
		msgbox.addButton("tbl", QMessageBox::AcceptRole);
		msgbox.addButton("txt", QMessageBox::AcceptRole);
		msgbox.addButton("csv", QMessageBox::AcceptRole);
		msgbox.setWindowModality(Qt::WindowModal);

		switch (msgbox.exec())
		{
		case 0: // tbl
			fileSize = writeAsTbl(bytesToWrite);
			break;
		case 1: // txt
			fileSize = writeAsText(bytesToWrite, false);
			break;
		case 2: // csv
			fileSize = writeAsText(bytesToWrite, true);
			break;
		}
	}

	QFile output(fileName);
	if (output.open(QIODevice::WriteOnly))
	{
		if (output.write(bytesToWrite) == fileSize)
		{
			_currentTableWidget->clearBackground();
			_lastPath = QFileInfo(fileName).canonicalPath();
			if (_openedTables == 1 || !inactiveNamedTableWidget(currentTablePanelWidget())->tableWidget()->isWindowModified())
				ui.actionSaveAll->setDisabled(true);

			updateWindow(false);
			ui.statusBar->showMessage(tr("File \"%1\" successfully saved").arg(fileName), 3000);

			return true;
		}
		else
			QMessageBox::critical(this, QApplication::applicationName(), tr("Error writing file \"%1\"\nReason: %2")
								  .arg(fileName, output.errorString()));
	}
	else
		QMessageBox::critical(this, QApplication::applicationName(), tr("Error creating file \"%1\"\nReason: %2")
							  .arg(fileName, output.errorString()));
	return false;
}

DWORD QTblEditor::writeAsTbl(QByteArray &bytesToWrite)
{
	int entriesNumber = _currentTableWidget->rowCount();
	QStringList stringValsWithModifiedColors; // replacing user-readable colors with their internal form
	for (WORD i = 0; i < entriesNumber; i++)
	{
		QString currentVal = _currentTableWidget->item(i, 1)->text();
		for (int j = 0; j < colorStrings.size(); j++)
		{
			if (currentVal.contains(colorStrings.at(j)) && j)
				currentVal.replace(colorStrings.at(j), colorHeader + colorCodes.at(j - 1));
			else
				currentVal.replace(colorStrings.at(j), colorHeader); // string "\color;" found
		}
		stringValsWithModifiedColors << currentVal;
	}

	DWORD dataStartOffset = TblHeader::size + entriesNumber*sizeof(WORD) + entriesNumber*TblHashNode::size;
	QVector<WORD> indices(entriesNumber);
	QVector<TblHashNode> nodes(entriesNumber);
	QVector<bool> collisionsDetected(entriesNumber, false);
	DWORD currentOffset = dataStartOffset, maxCollisionsNumber = 0;
	for (WORD i = 0; i < entriesNumber; i++)
	{
		QByteArray currentKey = _currentTableWidget->item(i, 0)->text().toLatin1(),
		currentVal = stringValsWithModifiedColors.at(i).toUtf8();
		DWORD hashValue = TblStructure::hashValue(currentKey.data(), entriesNumber), hashIndex = hashValue,
		currentCollisionsNumber = 0;
		while (collisionsDetected[hashIndex]) // counting collisions for current hash value
		{
			currentCollisionsNumber++;
			hashIndex++;
			hashIndex %= entriesNumber;
		}
		collisionsDetected[hashIndex] = true;
		if (currentCollisionsNumber > maxCollisionsNumber)
			maxCollisionsNumber = currentCollisionsNumber;
		indices[i] = hashIndex;

		// we need the size of UTF-8 data, not QString
		WORD currentKeyLength = qstrlen(currentKey.constData()) + 1, currentValLength = qstrlen(currentVal.constData()) + 1;
		// convenient constructor instead of nodes[hashIndex].Active = 1; ...
		nodes[hashIndex] = TblHashNode(1, i, hashValue, currentOffset, currentOffset + currentKeyLength, currentValLength);
		currentOffset += currentKeyLength + currentValLength;
	}

	bytesToWrite.fill('\0', TblHeader::size); // header will be filled in the end, so we reserve bytes for it
	QDataStream out(&bytesToWrite, QIODevice::WriteOnly);
	out.setByteOrder(QDataStream::LittleEndian);

	out.skipRawData(TblHeader::size); // current offset for writing = 0x15
	for (WORD i = 0; i < entriesNumber; i++)
		out << indices[i];

	for (WORD i = 0; i < entriesNumber; i++)
		out << nodes[i];

	for (WORD i = 0; i < entriesNumber; i++)
	{
		QByteArray currentKey = _currentTableWidget->item(i, 0)->text().toLatin1(),
			currentVal = stringValsWithModifiedColors.at(i).toUtf8();
		out.writeRawData(currentKey.constData(), qstrlen(currentKey.constData()) + 1);
		out.writeRawData(currentVal.constData(), qstrlen(currentVal.constData()) + 1);
	}

	DWORD fileSize = bytesToWrite.size();
	out.device()->reset(); // current offset for writing = 0x00 (to write the header)
	out << TblHeader(TblStructure::getCRC(bytesToWrite.data() + dataStartOffset, fileSize - dataStartOffset), entriesNumber,
		entriesNumber, 1, dataStartOffset, maxCollisionsNumber + 1, fileSize); // convenient constructor

	return fileSize;
}

DWORD QTblEditor::writeAsText(QByteArray &bytesToWrite, bool isCsv)
{
	int entriesNumber = _currentTableWidget->rowCount();
	QStringList stringValsWithModifiedNewLines; // all newlines in string values will look like \n
	for (WORD i = 0; i < entriesNumber; i++)
	{
		QString currentVal = _currentTableWidget->item(i, 1)->text();
		stringValsWithModifiedNewLines << currentVal.replace('\n', "\\n");
	}

	QTextStream out(&bytesToWrite, QIODevice::WriteOnly | QIODevice::Text);
	out.setCodec("UTF-8");          // generate *.txt in UTF-8 to avoid locale problems
	out.setAutoDetectUnicode(true); // or in Unicode

	// format: [wrapper]key[wrapper]<separator>[wrapper]value[wrapper]<newline>,
	// where <separator> is '\t' (for *.txt) or ','/';' (for *.csv),
	// [wrapper] - double quotes or nothing (depends on current settings).
	// for *.csv [wrapper] is always '\"'
	QString separator = "\t", wrappingChar = "\"";
	if (isCsv)
		separator = ui.actionCsvComma->isChecked() ? "," : ";";
	else if (!ui.actionWrapStrings->isChecked())
		wrappingChar = "";
	for (WORD i = 0; i < entriesNumber; i++)
		out << QString("%1%2%1%3%1%4%1\n").arg(wrappingChar, _currentTableWidget->item(i, 0)->text(), separator,
											   stringValsWithModifiedNewLines[i]);

	out.flush(); // if we don't flush buffer, fileSize in next line == 0
	return bytesToWrite.size();
}

void QTblEditor::showFindReplaceDialog()
{
	_findReplaceDlg->show(_openedTables == 2);
}

void QTblEditor::changeCurrentTableItem(QTableWidgetItem *newItem)
{
	_currentTableWidget = qobject_cast<D2StringTableWidget *>(newItem->tableWidget());
	_currentTableWidget->setCurrentCell(newItem->row(), newItem->column());

	currentTablePanelWidget()->setActive(true);
	if (_openedTables == 2)
		inactiveNamedTableWidget(currentTablePanelWidget())->setActive(false);
}

void QTblEditor::findNextString(const QString &query, bool isCaseSensitive, bool isExactString, bool isSearchBothTables)
{
	Qt::MatchFlags searchOptions(Qt::MatchWrap | Qt::MatchContains);
	if (isCaseSensitive)
		searchOptions |= Qt::MatchCaseSensitive;
	if (isExactString)
		searchOptions &= ~Qt::MatchContains;
	QList<QTableWidgetItem *> foundItems = _currentTableWidget->findItems(query, searchOptions);
	if (isSearchBothTables && _openedTables == 2)
		foundItems.append(inactiveTableWidget(_currentTableWidget)->findItems(query, searchOptions));
	emit searchFinished(foundItems);
}

void QTblEditor::goTo()
{
	GoToRowDialog dlg(this, _currentTableWidget->rowCount());
	if (dlg.exec())
		_currentTableWidget->setCurrentCell(dlg.row() - 1, 1);
}

void QTblEditor::recievingTextFromSingleItem(QTableWidgetItem *editedItem)
{
	KeyValueItemsPair dummyItemsPair(editedItem, 0);
	if (editedItem->tableWidget() == _leftTableWidget)
		recievingText(dummyItemsPair);
	else
		recievingText(emptyKeyValuePair, dummyItemsPair);
}

void QTblEditor::editString(QTableWidgetItem *itemToEdit)
{
	int row = itemToEdit->row();
	KeyValueItemsPair itemsPair;
	if (itemToEdit->column()) // value selected
		itemsPair = KeyValueItemsPair(itemToEdit->tableWidget()->item(row, 0), itemToEdit);
	else // key selected
		itemsPair = KeyValueItemsPair(itemToEdit, itemToEdit->tableWidget()->item(row, 1));

	EditStringCellDialog *editStringCellDlg = 0;
	if (_openedTables == 1 || row >= inactiveTableWidget(_currentTableWidget)->rowCount())
		editStringCellDlg = new EditStringCellDialog(this, itemsPair);
	else
	{
		QTableWidget *w = inactiveTableWidget(_currentTableWidget);
		KeyValueItemsPair otherItemsPair(w->item(row, 0), w->item(row, 1));
		if (_currentTableWidget == _leftTableWidget)
			editStringCellDlg = new EditStringCellDialog(this, itemsPair, otherItemsPair);
		else
			editStringCellDlg = new EditStringCellDialog(this, otherItemsPair, itemsPair);
	}

	connect(editStringCellDlg, SIGNAL(sendingText(KeyValueItemsPair, KeyValueItemsPair)), SLOT(recievingText(KeyValueItemsPair, KeyValueItemsPair)));
	connect(editStringCellDlg, SIGNAL(editorClosedAt(int)), _leftTableWidget, SLOT(changeCurrentCell(int)));
	if (_openedTables == 2)
	{
		connect(editStringCellDlg, SIGNAL(editorClosedAt(int)), _rightTableWidget, SLOT(changeCurrentCell(int)));
		connect(this, SIGNAL(tablesWereSwapped()), editStringCellDlg, SLOT(swapEditors()));
	}

	editStringCellDlg->show();
}

void QTblEditor::updateItem(QTableWidgetItem *item)
{
	if (item && item->background().color() != Qt::green)
	{
		item->setBackground(QBrush(Qt::green));

		TablePanelWidget *w = _leftTablePanelWidget->tableWidget() == item->tableWidget() ? _leftTablePanelWidget : _rightTablePanelWidget;
		w->tableWidget()->addEditedItem(item);
		w->setWindowModified(true);

		setWindowModified(true);
		ui.actionSave->setEnabled(true);
		ui.actionSaveAll->setEnabled(true);
		ui.actionReopen->setEnabled(w->absoluteFileName() != newTblFileName);
	}
}

void QTblEditor::recievingText(KeyValueItemsPair leftItemsPair, KeyValueItemsPair rightItemsPair)
{
	QList<QTableWidgetItem *> items = QList<QTableWidgetItem *>() << leftItemsPair.first << leftItemsPair.second
									  << rightItemsPair.first << rightItemsPair.second;
	foreach (QTableWidgetItem *item, items)
		updateItem(item);
}

bool QTblEditor::isDialogQuestionConfirmed(const QString &text)
{
	QMessageBox confirmationDialog(QMessageBox::Question, QApplication::applicationName(), text, QMessageBox::Yes | QMessageBox::No, this);
	confirmationDialog.setDefaultButton(QMessageBox::Yes);
#ifdef Q_OS_MAC
	confirmationDialog.setWindowModality(Qt::WindowModal);
#endif
	return confirmationDialog.exec() == QMessageBox::Yes;
}

void QTblEditor::deleteSelectedItems()
{
	bool needsClear = sender() == ui.actionClearSelected; // clear if true, delete if false
	if (isDialogQuestionConfirmed(tr("Are you sure you want to %1 selected items?").arg(needsClear ? tr("clear") : tr("delete"))))
	{
		_currentTableWidget->deleteItems(needsClear);
		updateWindow();
		if (!needsClear)
			currentTablePanelWidget()->updateRowCountLabel();
	}
}

void QTblEditor::updateWindow(bool isModified)
{
	setWindowModified(isModified);
	currentTablePanelWidget()->setWindowModified(isModified);

	ui.actionSave->setEnabled(isModified);
	if (currentTablePanelWidget()->absoluteFileName() == newTblFileName)
		ui.actionReopen->setEnabled(false);
	else
		ui.actionReopen->setEnabled(isModified);
}

void QTblEditor::updateLocationLabel(int newRow)
{
	if (newRow < 0)
		return;

	int row = newRow + 1, rows = _currentTableWidget->rowCount();
	_locationLabel->setText(QString("%1 (0x%2) / %3 (0x%4)").arg(row).arg(row, 0, 16).arg(rows).arg(rows, 0, 16));

	QString keyHash = QString("0x%1").arg(TblStructure::hashValue(_currentTableWidget->item(newRow, 0)->text().toUtf8().data(), rows), 0, 16);
	D2StringTableWidget *otherTableWidget = inactiveTableWidget(_currentTableWidget);
	int inactiveRows = otherTableWidget->rowCount();
	if (_openedTables == 2 && newRow < inactiveRows)
	{
		QString otherKeyHash = QString("0x%1").arg(TblStructure::hashValue(otherTableWidget->item(newRow, 0)->text().toUtf8().data(),
																		   inactiveRows), 0, 16);
		if (keyHash != otherKeyHash)
		{
			if (_currentTableWidget == _leftTableWidget)
				keyHash += " | " + otherKeyHash;
			else
				keyHash = otherKeyHash + " | " + keyHash;
		}
	}
	_keyHashLabel->setText(keyHash);
}

void QTblEditor::writeSettings()
{
	QSettings settings;

	settings.beginGroup("geometry");
	settings.setValue("windowGeometry", saveGeometry());
	settings.setValue("windowState", saveState());
	settings.setValue("splitterState", _tableSplitter->saveState());
	settings.endGroup();


	settings.beginGroup("options");
	settings.setValue("isCsvSeparatorComma", ui.actionCsvComma->isChecked());
	settings.setValue("restoreOpenedFiles", ui.actionRestoreLastOpenedFiles->isChecked());
	settings.setValue("wrapTxtStrings", ui.actionWrapStrings->isChecked());
	settings.setValue("showHexInRows", ui.actionShowHexInRow->isChecked());
	settings.setValue("startNumberingFrom", _startNumberingGroup->checkedAction()->text());
	settings.endGroup();


	settings.beginGroup("recentItems");
	settings.setValue("lastPath", _lastPath);
	settings.setValue("recentFiles", _recentFilesList);
	settings.endGroup();


	if (ui.actionRestoreLastOpenedFiles->isChecked() && _openedTables)
	{
		settings.beginGroup("lastSession");
		settings.setValue("firstTablePath", currentTablePanelWidget()->absoluteFileName());
		settings.setValue("firstTableRow", _currentTableWidget->currentRow());
		if (_openedTables == 2)
		{
			activateAnotherTable();
			settings.setValue("secondTablePath", currentTablePanelWidget()->absoluteFileName());
			settings.setValue("secondTableRow", _currentTableWidget->currentRow());
		}
		else
		{
			settings.remove("secondTablePath");
			settings.remove("secondTableRow");
		}
		settings.endGroup();
	}
	else
		settings.remove("lastSession");


	// write custom colors to file
	QFile f(
#ifdef Q_OS_MAC
			QApplication::applicationDirPath() + "/../Resources/" +
#endif
			customColorsFileName);
	if (f.open(QIODevice::WriteOnly))
	{
		QTextStream out(&f);
		out << tr("# You can place comments anywhere in the file\n# Format: name[tab]hex code[tab]hex RGB\n");
		for (int i = colorsNum; i < colors.size(); i++)
			out << colorStrings.at(i + 1) << '\t' << QString("0x%1").arg(colorCodes.at(i).unicode(), 0, 16)
			<< '\t' << colorHexString(colors.at(i)) << endl;
	}
	else
		QMessageBox::critical(this, QApplication::applicationName(),
							  tr("Unable to create file \"customcolors.ini\"\nReason: %1").arg(f.errorString()));
}

void QTblEditor::readSettings()
{
	QSettings settings;

	settings.beginGroup("geometry");
	restoreGeometry(settings.value("windowGeometry").toByteArray());
	restoreState(settings.value("windowState").toByteArray());
	_tableSplitter->restoreState(settings.value("splitterState").toByteArray());
	settings.endGroup();

	settings.beginGroup("options");
	ui.actionRestoreLastOpenedFiles->setChecked(settings.value("restoreOpenedFiles", true).toBool());
	ui.actionCsvSemiColon->setChecked(!settings.value("isCsvSeparatorComma", true).toBool());
	ui.actionWrapStrings->setChecked(settings.value("wrapTxtStrings", true).toBool());
	ui.actionShowHexInRow->setChecked(settings.value("showHexInRows").toBool());
	(settings.value("startNumberingFrom").toString() == "0" ? ui.actionStartNumberingFrom0 : ui.actionStartNumberingFrom1)->setChecked(true);
	settings.endGroup();

	settings.beginGroup("recentItems");
	_lastPath = settings.value("lastPath").toString();
	_recentFilesList = settings.value("recentFiles").toStringList();
	updateRecentFilesActions();
	settings.endGroup();

	settings.beginGroup("lastSession");
	QStringList keys = settings.allKeys();
	if (keys.length() >= 2)
	{
		if (loadFile(settings.value(keys.at(0)).toString()))
			_currentTableWidget->setCurrentCell(settings.value(keys.at(1)).toInt(), 1);

		if (keys.length() == 4)
		{
			if (loadFile(settings.value(keys.at(2)).toString(), false))
				_currentTableWidget->setCurrentCell(settings.value(keys.at(3)).toInt(), 1);
		}
	}
	settings.endGroup();


	// read custom colors from file
	QFile f(
#ifdef Q_OS_MAC
			QApplication::applicationDirPath() + "/../Resources/" +
#endif
			customColorsFileName);
	if (f.exists())
	{
		if (f.open(QIODevice::ReadOnly))
		{
			QTextStream in(&f);
			while (!in.atEnd())
			{
				QString line = in.readLine();
				if (!line.startsWith('#') && !line.isEmpty())
				{
					QStringList list = line.split('\t');
					if (list.size() != 3)
						continue;

					colorStrings.append(list.at(0));
					colorCodes.append(QChar(((QString)list.at(1)).toInt(0, 16)));

					QString colorString = list.at(2);
					int r = colorString.mid(1, 2).toInt(0, 16), g = colorString.mid(3, 2).toInt(0, 16), b = colorString.mid(5, 2).toInt(0, 16);
					colors.append(QColor(r, g, b));
				}
			}
		}
		else
			QMessageBox::critical(this, QApplication::applicationName(),
								  tr("Error opening file \"%1\"\nReason: %2").arg(f.fileName(), f.errorString()));
	}
}

void QTblEditor::aboutApp()
{
	QMessageBox::about(this, tr("About %1").arg(QApplication::applicationName()),
					   tr("%1 %2 (Special 'WarlordOfBlood' version)\nReleased: %3\n\nAuthor: Filipenkov Andrey (kambala)", "args 1 & 2 are app name and app version respectively")
					   .arg(QApplication::applicationName(), QApplication::applicationVersion(), releaseDate) +
					   "\nICQ: 287764961\nE-mail: decapitator@ukr.net");
}

TablePanelWidget *QTblEditor::currentTablePanelWidget() const
{
	return _currentTableWidget == _leftTablePanelWidget->tableWidget() ? _leftTablePanelWidget : _rightTablePanelWidget;
}

TablePanelWidget *QTblEditor::inactiveNamedTableWidget(TablePanelWidget *namedTableToCheck) const
{
	return namedTableToCheck == _leftTablePanelWidget ? _rightTablePanelWidget : _leftTablePanelWidget;
}

D2StringTableWidget *QTblEditor::inactiveTableWidget(D2StringTableWidget *tableToCheck) const
{
	return tableToCheck == _leftTableWidget ? _rightTableWidget : _leftTableWidget;
}

void QTblEditor::changeCurrentTable(QWidget *newActiveTable)
{
	TablePanelWidget *w = qobject_cast<TablePanelWidget *>(newActiveTable);
	if (_currentTableWidget != w->tableWidget())
	{
		_currentTableWidget = w->tableWidget();
		w->setActive(true);
		inactiveNamedTableWidget(w)->setActive(false);

		bool isTableModified = _currentTableWidget->isWindowModified();
		ui.actionSave->setEnabled(isTableModified);
		ui.actionReopen->setEnabled(isTableModified);
	}
}

void QTblEditor::activateAnotherTable()
{
	changeCurrentTable(inactiveNamedTableWidget(currentTablePanelWidget()));
	_currentTableWidget->setFocus();
}

void QTblEditor::supplement()
{
	D2StringTableWidget *smallerTable = _leftTableWidget->rowCount() < _rightTableWidget->rowCount() ? _leftTableWidget : _rightTableWidget;
	D2StringTableWidget *biggerTable = inactiveTableWidget(smallerTable);
	int i = smallerTable->rowCount(), maxRow = biggerTable->rowCount();
	if (i == maxRow)
		return;

	for (; i < maxRow; i++)
	{
		smallerTable->createRowAt(i);
		smallerTable->createNewEntry(i, biggerTable->item(i, 0)->text(), biggerTable->item(i, 1)->text());

		QTableWidgetItem *keyItem = smallerTable->item(i, 0), *stringItem = smallerTable->item(i, 1);
		keyItem->setBackgroundColor(Qt::green);
		stringItem->setBackgroundColor(Qt::green);
		smallerTable->addEditedItem(keyItem);
		smallerTable->addEditedItem(stringItem);
	}

	_currentTableWidget = smallerTable;
	updateWindow();
	ui.actionSaveAll->setEnabled(true);
}

void QTblEditor::swapTables()
{
	_tableSplitter->addWidget(_leftTablePanelWidget);

	TablePanelWidget *temp = _leftTablePanelWidget;
	_leftTablePanelWidget = _rightTablePanelWidget;
	_rightTablePanelWidget = temp;

	_leftTableWidget = _leftTablePanelWidget->tableWidget();
	_rightTableWidget = _rightTablePanelWidget->tableWidget();

	updateLocationLabel(_currentTableWidget->currentRow());
	emit tablesWereSwapped();
}

void QTblEditor::increaseRowCount(int rowIndex)
{
	_currentTableWidget->createRowAt(rowIndex);
	updateWindow();
	currentTablePanelWidget()->updateRowCountLabel();
}

void QTblEditor::copy()
{
	QString selectedText;
	foreach (QTableWidgetSelectionRange range, _currentTableWidget->selectedRanges())
		for (int j = range.topRow(); j <= range.bottomRow(); j++)
		{
			if (range.columnCount() == 1) // format: "text"<newline>
				selectedText += QString("\"%1\"\n").arg(_currentTableWidget->item(j, range.rightColumn())->text());
			else // format: "key"<tab>"value"<newline>
				selectedText += QString("\"%1\"\t\"%2\"\n").arg(_currentTableWidget->item(j, 0)->text(), _currentTableWidget->item(j, 1)->text());
		}

	QApplication::clipboard()->setText(selectedText);
}

void QTblEditor::paste()
{
	const QMimeData *data = QApplication::clipboard()->mimeData();
	if (data->hasText())
	{
		QStringList records = data->text().split("\"\n"); // strings can have more than 1 line
		if (((QString)records.last()).isEmpty()) // remove last empty line
			records.removeLast();
		int row = _currentTableWidget->currentRow() + 1, recordsNumber = records.size();
		if (((QString)records.at(0)).contains('\t')) // format: "key"<tab>"value"<newline>
		{
			for (int i = 0; i < recordsNumber; i++)
			{
				int rowIndex = row + i;
				_currentTableWidget->createRowAt(rowIndex);
				QStringList keyValueString = ((QString)records.at(i)).split('\t');
				_currentTableWidget->createNewEntry(rowIndex, keyValueString.at(0), keyValueString.at(1));
			}
		}
		else // format: "text"<newline>
		{
			bool isKey = !_currentTableWidget->currentColumn();
			for (int i = 0; i < recordsNumber; i++)
			{
				int rowIndex = row + i;
				_currentTableWidget->createRowAt(rowIndex);
				if (isKey)
					_currentTableWidget->createNewEntry(rowIndex, records.at(i), QString::null);
				else
					_currentTableWidget->createNewEntry(rowIndex, QString::null, records.at(i));
			}
		}

		for (int i = row; i < row + recordsNumber; i++)
		{
			updateItem(_currentTableWidget->item(i, 0));
			updateItem(_currentTableWidget->item(i, 1));
		}

		_currentTableWidget->setCurrentCell(row + recordsNumber, 0);
		currentTablePanelWidget()->updateRowCountLabel();
	}
}

void QTblEditor::toggleRowsHeight(bool isSmall)
{
	// TODO: try to improve performance
	int height = isSmall ? 20 : 30;
	for (int i = 0; i < _leftTableWidget->rowCount(); i++)
		_leftTableWidget->setRowHeight(i, height);
	for (int i = 0; i < _rightTableWidget->rowCount(); i++)
		_rightTableWidget->setRowHeight(i, height);
}

QStringList QTblEditor::differentStrings(TablesDifferencesWidget::DiffType diffType) const
{
	int minRows = qMin(_leftTableWidget->rowCount(), _rightTableWidget->rowCount());
	QStringList differenceRows;
	for (int i = 0; i < minRows; i++)
	{
		bool areDifferentKeys = _leftTableWidget->item(i, 0)->text() != _rightTableWidget->item(i, 0)->text();
		bool areDifferentStrings = !areDifferentKeys && _leftTableWidget->item(i, 1)->text() != _rightTableWidget->item(i, 1)->text();
		if (diffType == TablesDifferencesWidget::Keys && areDifferentKeys ||
			diffType == TablesDifferencesWidget::Strings && areDifferentStrings ||
			diffType == TablesDifferencesWidget::KeysOrStrings && (areDifferentKeys || areDifferentStrings) ||
			diffType == TablesDifferencesWidget::SameStrings && !areDifferentKeys && !areDifferentStrings)
				differenceRows.append(QString("%1 (0x%2)").arg(i + 1).arg(i + 1, 0, 16));
	}
	return differenceRows;
}

void QTblEditor::showDifferences()
{
	QAction *action = qobject_cast<QAction *>(sender());
	TablesDifferencesWidget::DiffType diffType = (TablesDifferencesWidget::DiffType)action->data().toInt();

	QStringList differenceRows = differentStrings(diffType);
	if (differenceRows.size())
	{
		TablesDifferencesWidget *diffWidget = 0;
		foreach (TablesDifferencesWidget *w, findChildren<TablesDifferencesWidget *>())
		{
			if (w->diffType() == diffType)
			{
				diffWidget = w;
				diffWidget->clear();
				break;
			}
		}

		if (!diffWidget)
		{
			diffWidget = new TablesDifferencesWidget(this, diffType);
			connect(diffWidget, SIGNAL(rowDoubleClicked(QListWidgetItem *)), _leftTableWidget, SLOT(listWidgetItemDoubleClicked(QListWidgetItem *)));
			connect(diffWidget, SIGNAL(refreshRequested(TablesDifferencesWidget *)), SLOT(refreshDifferences(TablesDifferencesWidget *)));
		}
		diffWidget->addRows(differenceRows);
		diffWidget->resize(diffWidget->sizeHint());
		diffWidget->show();
	}
	else
		QMessageBox::information(this, QApplication::applicationName(), tr("Tables are identical"));
}

void QTblEditor::refreshDifferences(TablesDifferencesWidget *w)
{
	w->clear();
	w->addRows(differentStrings(w->diffType()));
}