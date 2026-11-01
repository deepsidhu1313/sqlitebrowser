#include "ComboWheelFilter.h"

#include <QApplication>
#include <QComboBox>
#include <QEvent>

namespace {

// Marks the combo boxes whose focus policy was changed by the filter, so it can be restored later
const char* const ChangedFocusPolicy = "ComboWheelFilterChangedFocusPolicy";

// Combo boxes take the focus when the mouse wheel is turned over them, before the wheel event is even delivered.
// So they would always have the focus when the event arrives. They have to be reached by click or tab key instead.
void disableWheelFocus(QComboBox* combo)
{
    if(combo->focusPolicy() == Qt::WheelFocus)
    {
        combo->setFocusPolicy(Qt::StrongFocus);
        combo->setProperty(ChangedFocusPolicy, true);
    }
}

void restoreWheelFocus(QComboBox* combo)
{
    if(combo->property(ChangedFocusPolicy).toBool())
    {
        combo->setFocusPolicy(Qt::WheelFocus);
        combo->setProperty(ChangedFocusPolicy, QVariant());
    }
}

}

ComboWheelFilter::ComboWheelFilter(QObject* parent) :
    QObject(parent),
    m_active(false)
{
}

void ComboWheelFilter::setActive(bool active)
{
    if(active == m_active)
        return;
    m_active = active;

    // The combo boxes which exist already are not going to be polished again, so take care of them now
    const QWidgetList widgets = QApplication::allWidgets();
    for(QWidget* widget : widgets)
    {
        if(QComboBox* combo = qobject_cast<QComboBox*>(widget))
        {
            if(m_active)
                disableWheelFocus(combo);
            else
                restoreWheelFocus(combo);
        }
    }
}

bool ComboWheelFilter::eventFilter(QObject* obj, QEvent* event)
{
    if(!m_active)
        return false;

    switch(event->type())
    {
    case QEvent::Polish:
        // This is the last chance to take care of combo boxes created while the filter is active before they are shown
        if(QComboBox* combo = qobject_cast<QComboBox*>(obj))
            disableWheelFocus(combo);
        break;
    case QEvent::Wheel:
        // Only the combo boxes are looked at, not the lists popping up from them, so these can still be scrolled.
        // Ignoring the event passes it on to the parent widget, e.g. a scroll area.
        if(QComboBox* combo = qobject_cast<QComboBox*>(obj))
        {
            if(!combo->hasFocus())
            {
                event->ignore();
                return true;
            }
        }
        break;
    default:
        break;
    }

    return false;
}
