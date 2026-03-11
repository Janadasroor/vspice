void SimulationPanel::addProbe(const QString& signalName) {
    if (signalName.isEmpty()) return;
    
    // Check if it already exists
    for (int i = 0; i < m_signalList->count(); ++i) {
        if (m_signalList->item(i)->text() == signalName) {
            m_signalList->item(i)->setCheckState(Qt::Checked);
            return;
        }
    }
    
    QListWidgetItem* item = new QListWidgetItem(signalName);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Checked);
    m_signalList->addItem(item);
    
    m_logOutput->append(QString("📍 Probed signal: %1").arg(signalName));
}
