#include "filemanager.h"
#include "polarplot.h"

#include <limits>
#include <algorithm>

#include <QtConcurrent>
#include <QPainter>
#include <QtMath>
#include <QVector>

PING_LOGGING_CATEGORY(polarplot, "ping.polarplot")

// Number of samples to display
uint16_t PolarPlot::_angularResolution = 400;

PolarPlot::PolarPlot(QQuickItem *parent)
    :Waterfall(parent)
    ,_distances(_angularResolution, 0)
    ,_image(2500, 2500, QImage::Format_RGBA8888)
    ,_maxDistance(0)
    ,_painter(nullptr)
    ,_updateTimer(new QTimer(this))
{
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    _image.fill(QColor(Qt::transparent));

    connect(_updateTimer, &QTimer::timeout, this, [&] {update();});
    _updateTimer->setSingleShot(true);
    _updateTimer->start(50);

    connect(this, &Waterfall::mousePosChanged, this, &PolarPlot::updateMouseColumnData);
}

void PolarPlot::clear()
{
    qCDebug(polarplot) << "Cleaning waterfall and restarting internal variables";
    _image.fill(Qt::transparent);
    _distances.fill(0, _angularResolution);
    _maxDistance = 0;
}

void PolarPlot::paint(QPainter *painter)
{
    static QPixmap pix;
    if(painter != _painter) {
        _painter = painter;
    }

    // http://blog.qt.io/blog/2006/05/13/fast-transformed-pixmapimage-drawing/
    pix = QPixmap::fromImage(_image, Qt::NoFormatConversion);
    _painter->drawPixmap(QRect(0, 0, width(), height()), pix, QRect(0, 0, _image.width(), _image.height()));
}

void PolarPlot::setImage(const QImage &image)
{
    _image = image;
    emit imageChanged();
    setImplicitWidth(image.width());
    setImplicitHeight(image.height());
}

void PolarPlot::draw(const QVector<double>& points, float angle, float initPoint, float length, float angleGrad)
{
    Q_UNUSED(initPoint)
    Q_UNUSED(length)

    static const QPoint center(_image.width()/2, _image.height()/2);
    constexpr float degreeToRadian = M_PI/180.0f;
    constexpr float gradianToDegree = 180.0f/200.0f;
    constexpr float gradianToRadian = M_PI/200.0f;
    constexpr float angleResolution = gradianToDegree / 2;

    const float resultionTimesAngleGradTimes2 = 2 * angleResolution * angleGrad;
    const float degreeToRadianTimesAngleGradTimes2 = 2 * degreeToRadian * angleGrad;
    static QColor pointColor;
    static float step;
    static float angleStep;
    float actualAngle = angle*gradianToRadian;

    //TODO: Need a better way to deal with dynamic steps, maybe doing `draw(data, angle++)` with `angleGrad` loop
    _distances[static_cast<int>(angle) % _angularResolution] = initPoint + length;

    float maxDistance = *std::max_element(std::begin(_distances), std::end(_distances));

    if(!qFuzzyCompare(maxDistance,_maxDistance)) {
        _maxDistance = maxDistance;
        emit maxDistanceChanged();
    }

    const float linearFactor = points.size() / (float) center.x();
    for(int i = 1; i < center.x(); i++) {
        if(i < center.x() * length / _maxDistance) {
            pointColor = valueToRGB(points[static_cast<int>( i * linearFactor - 1)]);
        } else {
            pointColor = QColor(0, 0, 0, 0);
        }

        step = ceil(i * degreeToRadianTimesAngleGradTimes2);
        float halfstep = step / 2;
        float deltaDrgree_A = (resultionTimesAngleGradTimes2 / (float) step);
        // The math and logic behind this loop is done in a way that the interaction is done with ints
        for(int currentStep = 0; currentStep < step; currentStep++) {
            float deltaDegree = deltaDrgree_A * (currentStep - halfstep);
            angleStep = deltaDegree * degreeToRadian + actualAngle - M_PI_2;
            _image.setPixelColor(center.x() + i * cos(angleStep), center.y() + i * sin(angleStep), pointColor);
        }
    }

    // Fix max update in 20Hz at max
    if(!_updateTimer->isActive()) {
        _updateTimer->start(50);
    }
}

void PolarPlot::updateMouseColumnData()
{
    constexpr float rad2grad = 200.0f/M_PI;
    constexpr float grad2deg = 180.0f/200.0f;
    const QPointF center(width()/2, height()/2);

    /**
     * @brief delta is a normalized value with x,y ∈ [-1, 1]
     * Where (0, 0) represents the center of the circle of `radius = 1`
     *
     *  radius_{real} = \frac{max(height, width)}{2}
     *  \delta = point/radius_{real}
     */
    const QPointF delta = 2*(_mousePos - center)/std::min(width(), height());

    // Check if mouse is inside circle
    if(hypotf(delta.x(), delta.y()) > 1) {
        _containsMouse = false;
        emit containsMouseChanged();
        return;
    }

    // Calculate the angle in degrees
    int grad = static_cast<int>(atan2f(-delta.x(), delta.y())*rad2grad + 200) % 400;
    _mouseSampleAngle = grad*grad2deg;

    // Calculate mouse distance in meters
    _mouseSampleDistance = std::hypotf(delta.x(), delta.y())*_maxDistance*1e-3;

    emit mouseSampleAngleChanged();
    emit mouseSampleDistanceChanged();
}
