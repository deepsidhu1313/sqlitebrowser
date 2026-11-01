#ifndef COMBOWHEELFILTER_H
#define COMBOWHEELFILTER_H

#include <QObject>

// Application wide event filter which keeps the mouse wheel from changing the selection of drop-down lists
// that don't have the keyboard focus. Without this, scrolling a dialog or a panel with the mouse wheel
// changes the value of every combo box the pointer happens to pass over.
//
// While active, a combo box only reacts to the mouse wheel after it has been clicked or tabbed to; the wheel
// is passed on to the surrounding scroll area otherwise. The lists popping up from combo boxes are not affected.
// It does nothing unless it has been activated, so the standard Qt behaviour is the default.
class ComboWheelFilter : public QObject
{
    Q_OBJECT

public:
    explicit ComboWheelFilter(QObject* parent = nullptr);

    bool isActive() const { return m_active; }
    void setActive(bool active);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    bool m_active;
};

#endif
