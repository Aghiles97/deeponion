// Copyright (c) 2011-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/overviewpage.h>
#include <qt/forms/ui_overviewpage.h>

#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodel.h>

#include <QAbstractItemDelegate>
#include <QPainter>

#define DECORATION_SIZE 54
#define NUM_ITEMS 8

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    explicit TxViewDelegate(const PlatformStyle *_platformStyle, QObject *parent=nullptr):
        QAbstractItemDelegate(parent), unit(BitcoinUnits::BTC),
        platformStyle(_platformStyle)
    {

    }

    inline void paint(QPainter *painter, const QStyleOptionViewItem &option,
                      const QModelIndex &index ) const
    {
        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(TransactionTableModel::RawDecorationRole));
        QRect mainRect = option.rect;
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE, DECORATION_SIZE));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2*ypad)/2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top()+ypad, mainRect.width() - xspace, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top()+ypad+halfheight, mainRect.width() - xspace, halfheight);
        icon = platformStyle->SingleColorIcon(icon);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = option.palette.color(QPalette::Text);
        if(value.canConvert<QBrush>())
        {
            QBrush brush = qvariant_cast<QBrush>(value);
            foreground = brush.color();
        }

        painter->setPen(foreground);
        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft|Qt::AlignVCenter, address, &boundingRect);

        if (index.data(TransactionTableModel::WatchonlyRole).toBool())
        {
            QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
            QRect watchonlyRect(boundingRect.right() + 5, mainRect.top()+ypad+halfheight, 16, halfheight);
            iconWatchonly.paint(painter, watchonlyRect);
        }

        if(amount < 0)
        {
            foreground = COLOR_NEGATIVE;
        }
        else if(!confirmed)
        {
            foreground = COLOR_UNCONFIRMED;
        }
        else
        {
            foreground = option.palette.color(QPalette::Text);
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true, BitcoinUnits::separatorAlways);
        if(!confirmed)
        {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight|Qt::AlignVCenter, amountText);

        painter->setPen(option.palette.color(QPalette::Text));
        painter->drawText(amountRect, Qt::AlignLeft|Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
    const PlatformStyle *platformStyle;

};
#include <qt/overviewpage.moc>

OverviewPage::OverviewPage(const PlatformStyle *_platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    currentStakeBalance(-1),
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchImmatureBalance(-1),
    currentWatchStakeBalance(-1),
    txdelegate(new TxViewDelegate(_platformStyle, this)),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    // use a SingleColorIcon for the "out of sync warning" icon
    QIcon icon = platformStyle->SingleColorIcon(":/icons/warning");
    icon.addPixmap(icon.pixmap(QSize(64,64), QIcon::Normal), QIcon::Disabled); // also set the disabled icon because we are using a disabled QPushButton to work around missing HiDPI support of QLabel (https://bugreports.qt.io/browse/QTBUG-42503)

    ui->wallet_summary->setStyleSheet(platformStyle->getThemeManager()->getCurrent()->getQFrameGeneralStyle());
    ui->page_title->setStyleSheet(platformStyle->getThemeManager()->getCurrent()->getMainHeaderStyle());
    ui->walletSummaryHeader->setStyleSheet(platformStyle->getThemeManager()->getCurrent()->getSubSectionTitleStyle());
    ui->do_icon->setIcon(QIcon(platformStyle->getThemeManager()->getCurrent()->getDeepOnionLogo()));

    // DeepOnion: Style up the Transaction page.
    ui->listTransactions->setStyleSheet(platformStyle->getThemeManager()->getCurrent()->getQTableGeneralStyle());
    ui->listTransactions->horizontalHeader()->setStyleSheet(platformStyle->getThemeManager()->getCurrent()->getQListHeaderGeneralStyle());

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
    connect(ui->labelWalletStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
//    connect(ui->labelTransactionsStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& stakeBalance, const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance, const CAmount& watchStakeBalance)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    currentStakeBalance = stakeBalance;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchImmatureBalance = watchImmatureBalance;
    currentWatchStakeBalance = watchStakeBalance;
    ui->labelBalance->setText(BitcoinUnits::format(unit, balance, false, BitcoinUnits::separatorAlways));
    ui->labelUnconfirmed->setText(BitcoinUnits::format(unit, unconfirmedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelImmature->setText(BitcoinUnits::format(unit, immatureBalance, false, BitcoinUnits::separatorAlways));
    ui->labelStake->setText(BitcoinUnits::format(unit, stakeBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::format(unit, balance + unconfirmedBalance + immatureBalance + stakeBalance, false, BitcoinUnits::separatorAlways));
    ui->labelBalanceUnit->setText(BitcoinUnits::shortName(unit));
    ui->labelUnconfirmedUnit->setText(BitcoinUnits::shortName(unit));
    ui->labelImmatureUnit->setText(BitcoinUnits::shortName(unit));
    ui->labelStakeUnit->setText(BitcoinUnits::shortName(unit));
    ui->labelTotalUnit->setText(BitcoinUnits::shortName(unit));
    
// TODO
//    ui->labelWatchAvailable->setText(BitcoinUnits::formatWithUnit(unit, watchOnlyBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelWatchPending->setText(BitcoinUnits::formatWithUnit(unit, watchUnconfBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelWatchImmature->setText(BitcoinUnits::formatWithUnit(unit, watchImmatureBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelWatchStake->setText(BitcoinUnits::formatWithUnit(unit, watchStakeBalance, false, BitcoinUnits::separatorAlways));
//    ui->labelWatchTotal->setText(BitcoinUnits::formatWithUnit(unit, watchOnlyBalance + watchUnconfBalance + watchImmatureBalance + watchStakeBalance, false, BitcoinUnits::separatorAlways));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    bool showStake = stakeBalance != 0;
    bool showWatchOnlyImmature = watchImmatureBalance != 0;
    bool showWatchOnlyStake = watchStakeBalance != 0;

    // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelImmature->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelImmatureUnit->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelStake->setVisible(showStake || showWatchOnlyStake);
    ui->labelStakeText->setVisible(showStake || showWatchOnlyStake);
    ui->labelStakeUnit->setVisible(showStake || showWatchOnlyStake);
    
// TODO
//    ui->labelWatchImmature->setVisible(showWatchOnlyImmature); // show watch-only immature balance
//    ui->labelWatchStake->setVisible(showWatchOnlyStake); // show watch-only stake balance

}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
// TODO
//    ui->labelSpendable->setVisible(showWatchOnly);      // show spendable label (only when watch-only is active)
//    ui->labelWatchonly->setVisible(showWatchOnly);      // show watch-only label
//    ui->lineWatchBalance->setVisible(showWatchOnly);    // show watch-only balance separator line
//    ui->labelWatchAvailable->setVisible(showWatchOnly); // show watch-only available balance
//    ui->labelWatchPending->setVisible(showWatchOnly);   // show watch-only pending balance
//    ui->labelWatchTotal->setVisible(showWatchOnly);     // show watch-only total balance
//
//    if (!showWatchOnly)
//    {
//        ui->labelWatchImmature->hide();
//        ui->labelWatchStake->hide();
//    }
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter.get());
        ui->listTransactions->setAlternatingRowColors(true);
        ui->listTransactions->setSortingEnabled(true);
        ui->listTransactions->sortByColumn(TransactionTableModel::Date, Qt::DescendingOrder);
        ui->listTransactions->verticalHeader()->hide();
        ui->listTransactions->horizontalHeader()->resizeSection(TransactionTableModel::Status, 28);
        ui->listTransactions->horizontalHeader()->resizeSection(TransactionTableModel::Watchonly, 28);
        ui->listTransactions->horizontalHeader()->resizeSection(TransactionTableModel::Date, 120);
        ui->listTransactions->horizontalHeader()->resizeSection(TransactionTableModel::Type, 120);
        ui->listTransactions->horizontalHeader()->setSectionResizeMode(TransactionTableModel::ToAddress, QHeaderView::Stretch);
        ui->listTransactions->horizontalHeader()->resizeSection(TransactionTableModel::Amount, 120);

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(), model->getStakeBalance(),
                   model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance(), model->getWatchStakeBalance());
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount )), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

        updateWatchOnlyLabels(model->haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));
    }

    // update the display unit, to not use the default ("BTC")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if(currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance, currentStakeBalance,
                       currentWatchOnlyBalance, currentWatchUnconfBalance, currentWatchImmatureBalance, currentWatchStakeBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = walletModel->getOptionsModel()->getDisplayUnit();

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString &warnings)
{
//    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
//    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
//    ui->labelTransactionsStatus->setVisible(fShow);
}

void OverviewPage::refreshStyle()
{
    ui->wallet_summary->setStyleSheet(platformStyle->getThemeManager()->getCurrent()->getQFrameGeneralStyle());
    ui->page_title->setStyleSheet(platformStyle->getThemeManager()->getCurrent()->getMainHeaderStyle());
    ui->walletSummaryHeader->setStyleSheet(platformStyle->getThemeManager()->getCurrent()->getSubSectionTitleStyle());
    ui->do_icon->setIcon(QIcon(platformStyle->getThemeManager()->getCurrent()->getDeepOnionLogo()));

    // DeepOnion: Style up the Transaction page.
    ui->listTransactions->setStyleSheet(platformStyle->getThemeManager()->getCurrent()->getQTableGeneralStyle());
    ui->listTransactions->horizontalHeader()->setStyleSheet(platformStyle->getThemeManager()->getCurrent()->getQListHeaderGeneralStyle());
}
