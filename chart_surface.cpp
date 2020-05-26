#include "chart_surface.hpp"

#include <QApplication>
#include <QMouseEvent>
#include <QPoint>
#include <QMenu>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include "common/date.hpp"

const float toolkit::chart_surface::c_mouse_move_sensitivity = 0.01f;
const float toolkit::chart_surface::c_mouse_rotation_sensitivity = 0.1f;
const float toolkit::chart_surface::c_scroll_sensitivity = 830.0f;

toolkit::chart_surface::chart_surface(toolkit_dlg& parent, const std::shared_ptr<const surface>& data) :
                                                          chart_base(parent) // base class
                                                        , m_surface_data(data)
                                                        , m_elevation(20)
                                                        , m_rotation(30)
                                                        , m_zoom(1)
                                                        , m_dx(0)
                                                        , m_dy(0)
                                                        , m_mouse_position(QCursor::pos())
                                                        , m_mouse_button_pressed(false)
                                                        , m_x_slice(parent, *this, X) // NOTE: you can't find these charts in toolkit_dlg::m_charts. It's internal
                                                        , m_y_slice(parent, *this, Y)
                                                        , m_x_slice_action(((std::string)"Slice by "+data->m_prefs.x_axix_name).c_str(), this)
                                                        , m_y_slice_action(((std::string)"Slice by "+data->m_prefs.y_axix_name).c_str(), this)
                                                        , m_signalMapper(this)
{
    QObject::connect(&m_chart_viewer, SIGNAL(mouseMovePlotArea(QMouseEvent*)), SLOT(on_mouse_move_plot_area(QMouseEvent*)));
    QObject::connect(&m_chart_viewer, SIGNAL(mouseWheel(QWheelEvent*)), SLOT(on_mouse_wheel_event_slot(QWheelEvent*)));
    QObject::connect(&m_chart_viewer, SIGNAL(clicked(QMouseEvent*)), SLOT(on_mouse_click(QMouseEvent*)));

    QWidget::addAction(&m_x_slice_action);
    QWidget::addAction(&m_y_slice_action);
    QObject::connect(&m_x_slice_action, SIGNAL(triggered()), &m_signalMapper, SLOT(map()));
    QObject::connect(&m_y_slice_action, SIGNAL(triggered()), &m_signalMapper, SLOT(map()));
    m_signalMapper.setMapping(&m_x_slice_action, X);
    m_signalMapper.setMapping(&m_y_slice_action, Y);
    QObject::connect(&m_signalMapper, SIGNAL(mapped(int)), SLOT(on_make_slice(int)));
}

void toolkit::chart_surface::resizeEvent(QResizeEvent* event)
{
    chart_base::resizeEvent(event);

    // place X- and Y- slice at the right part of window
    const int size = event->size().height()/2;
    const int left = event->size().width();
    m_x_slice.resize(size, size);
    m_y_slice.resize(size, size);
    m_x_slice.move(left-size, 0);
    m_y_slice.move(left-size, size);
}

void toolkit::chart_surface::mouseReleaseEvent(QMouseEvent* event)
{
    QMenu menu(this);
    menu.addActions(this->actions());
    menu.exec(event->globalPos());
}

// it's being called during rotation and resize of window
void toolkit::chart_surface::redraw_chart()
{
    std::auto_ptr<SurfaceChart> chart(construct_surface()); // we use auto_ptr to free memory after use
    m_chart_viewer.setChart(chart.get()); // we use auto_ptr to free memory after use
    m_chart_viewer.setImageMap(nullptr); // there is no any tooltip for SurfaceChart
    m_chart_viewer.setVisible(true);
}

// chart update during rotation
void toolkit::chart_surface::redraw_chart(float elevation, float rotation, float move_dx, float move_dy, float dzoom)
{
    if (m_visible_area)
    {
        float dx = -move_dy*m_zoom;
        float dy = move_dx*m_zoom;
        m_dx += (dx*cos(m_elevation/360) + dy*sin(m_rotation/360))*m_visible_area->width;
        m_dy += (dx*sin(m_elevation/360) + dy*cos(m_rotation/360))*m_visible_area->height;
    }
    m_elevation += elevation;
    m_rotation -= rotation;
    m_zoom *= dzoom;
    if (m_zoom > 1)
        m_zoom = 1;

    redraw_chart();
}

std::auto_ptr<SurfaceChart> toolkit::chart_surface::construct_surface()
{
    const int chart_width = width()-height()/2; // Note that slices are on the right of window

    const double* dataX = &m_surface_data->m_x[0];
    const double* dataY = &m_surface_data->m_y[0];
    const double* dataZ = &m_surface_data->m_z[0];

    // Create a SurfaceChart object
    std::auto_ptr<SurfaceChart> chart(new SurfaceChart(chart_width, height()));

    // Add a title to the chart using 20 points Times New Roman Italic font
    chart->addTitle(m_surface_data->m_prefs.chart_title.c_str(), nullptr, 20);

    // Set the x, y and z axis titles using 10 points Arial Bold font
    chart->xAxis()->setTitle(m_surface_data->m_prefs.x_axix_name.c_str(), nullptr, 10);
    chart->yAxis()->setTitle(m_surface_data->m_prefs.y_axix_name.c_str(), nullptr, 10);
    chart->zAxis()->setTitle(m_surface_data->m_prefs.z_axix_name.c_str(), nullptr, 10);

    // adjust axis label for some data types
    if (toolkit_prefs_surface::term_structure_data_type==m_surface_data->m_prefs.chart_type
     || toolkit_prefs_surface::volatility_surface_data_type==m_surface_data->m_prefs.chart_type)
        chart->xAxis()->setLabelFormat("{value|mm/dd/yy}"); // don't want to view hh/mm/ss

    // Set the center of the plot region and set width x depth x height
    chart->setPlotRegion(chart_width/2, height()/2 // coordinates of the point of the image at which the center of the plot region is located
                       , chart_width/2, chart_width/2, height()/2); // the length of the dimensions of the plot region (X, Y, Z) in pixels

    // Set the elevation and rotation angles
    chart->setViewAngle(m_elevation, m_rotation);

    // set chart's scale
    if (!m_visible_area)
    {
        double minX = *std::min_element(dataX, dataX+m_surface_data->m_x.size());
        double minY = *std::min_element(dataY, dataY+m_surface_data->m_y.size());
        double maxX = *std::max_element(dataX, dataX+m_surface_data->m_x.size());
        double maxY = *std::max_element(dataY, dataY+m_surface_data->m_y.size());
        m_visible_area = position_type((minX+maxX)/2, (minY+maxY)/2, maxX-minX, maxY-minY);
    }
    chart->xAxis()->setLinearScale(m_visible_area->center_x+m_dx-m_visible_area->width*m_zoom/2, m_visible_area->center_x+m_dx+m_visible_area->width*m_zoom/2);
    chart->yAxis()->setLinearScale(m_visible_area->center_y+m_dy-m_visible_area->height*m_zoom/2, m_visible_area->center_y+m_dy+m_visible_area->height*m_zoom/2);

    // Set the data to use to plot the chart
    chart->setData(
        DoubleArray(dataX, m_surface_data->m_x.size()),
        DoubleArray(dataY, m_surface_data->m_y.size()),
        DoubleArray(dataZ, m_surface_data->m_z.size()));

    // Spline interpolate data to a 80 x 80 grid for a smooth surface
    chart->setInterpolation(80, 80);

    // Set surface grid lines to semi-transparent black (dd000000)
    chart->setSurfaceAxisGrid(0xdd000000);

    // Set contour lines to semi-transparent white (80ffffff)
    chart->setContourColor(0x80ffffff);

    // Add a color axis (the legend)
    // Set the length to 200 pixels and the labels on the right side. Use smooth gradient coloring.
    chart->setColorAxis(width()-75, height()/2, Chart::Left, 200, Chart::Right)->setColorGradient(); // TODO: may be make for other charts?

    // Output the chart
    chart->makeChart();

    return chart;
}

void toolkit::chart_surface::on_mouse_move_plot_area(QMouseEvent* mouse_event)
{
    if (QApplication::mouseButtons() & Qt::LeftButton)
    {
        if (m_mouse_button_pressed)
        {
            const QPoint p = QCursor::pos();

            float dx = p.y()-m_mouse_position.y();
            float dy = p.x()-m_mouse_position.x();
            // move chart area or rotate it
            if (QApplication::keyboardModifiers() & Qt::ControlModifier) // if keyboard key is pressed
                redraw_chart(0, 0, dx*c_mouse_move_sensitivity, dy*c_mouse_move_sensitivity, 1); // redraw chart
            else
                redraw_chart(dx*c_mouse_rotation_sensitivity, dy*c_mouse_rotation_sensitivity, 0, 0, 1); // redraw chart
            m_mouse_position = p;
        }
        else
        {
            m_mouse_button_pressed = true;
            m_mouse_position = QCursor::pos();
        }
    }
    else
        m_mouse_button_pressed = false;
}

void toolkit::chart_surface::on_mouse_wheel_event_slot(QWheelEvent* wheel_event)
{
    int delta = wheel_event->delta();
    redraw_chart(0, 0, 0, 0, exp(-(float)delta/c_scroll_sensitivity));
}

void toolkit::chart_surface::on_mouse_click(QMouseEvent* mouse_event)
{
    if (Qt::RightButton==mouse_event->button())
    {
        QMenu menu(this);
        menu.addActions(this->actions());
        menu.exec(mouse_event->globalPos());
    }
}

void toolkit::chart_surface::on_make_slice(int axis)
{
    switch (axis)
    {
    case X:
        // We set 'delete object on close'
        new slice_params_dlg(*this, X, m_surface_data->m_prefs.x_axix_name, "Make slice!");
        break;
    case Y:
        // We set 'delete object on close'
        new slice_params_dlg(*this, Y, m_surface_data->m_prefs.y_axix_name, "Make slice!");
        break;
    }
    return;
}

double toolkit::chart_surface::convert_string(const std::string& value)
{
    boost::smatch results;

    // try to convert to date format 'd/m/y"
    boost::regex date_str("(\\d+)/(\\d+)/(\\d+)");
    if (boost::regex_match(value, results, date_str))
    {
        int year = 2000+boost::lexical_cast<int>(results[3]);
        int month = boost::lexical_cast<int>(results[2]);
        int day = boost::lexical_cast<int>(results[1]);
        return Chart::chartTime(year, month, day);
    }

    // try to convert to double
    std::string adjusted_value(value);
    std::replace(adjusted_value.begin(), adjusted_value.end(), ',', '.');

    boost::regex double_str("(\\d+)([.](\\d+))?");
    if (boost::regex_match(adjusted_value, results, double_str))
        return boost::lexical_cast<double>(adjusted_value.c_str());

    throw "convert_string: can't make conversion";
}

void toolkit::chart_surface::make_slice(axis_type axis, const std::string& value)
{
    double converted_value;
    try
    {
        converted_value = convert_string(value);
    }
    catch(...)
    {
        return;
    }

    surface_slice* slice = new surface_slice();
    slice->surface_id = m_surface_data->get_uid();
    slice->axis = axis;
    slice->m_prefs = toolkit::slice_prefs(axis);
    slice->slice_value = converted_value;
    if (X==axis)
        m_x_slice.add_surface_slice_data(std::shared_ptr<surface_slice>(slice));
    else if (Y==axis)
        m_y_slice.add_surface_slice_data(std::shared_ptr<surface_slice>(slice));
    else
        throw std::string("Can make slices only for X and Y axes");
}

toolkit::slice_params_dlg::slice_params_dlg(chart_surface& surface, axis_type axis, const std::string& label_text, const std::string& button_text) : QDialog(&surface)
                    , m_surface(surface)
                    , m_axis(axis)
                    , m_slice_dlg_label(label_text.c_str())
                    , m_slice_dlg_button(button_text.c_str())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::WindowModal);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle("Slice parameters");
    setFixedSize(500, 100);

    QObject::connect(&m_slice_dlg_button, SIGNAL(clicked()), SLOT(on_make_slice()));

    m_slice_dlg_layout.addWidget(&m_slice_dlg_label);
    m_slice_dlg_layout.addWidget(&m_slice_dlg_edit);
    m_slice_dlg_layout.addWidget(&m_slice_dlg_button);
    setLayout(&m_slice_dlg_layout);
    show();
}

void toolkit::slice_params_dlg::on_make_slice()
{
    m_surface.make_slice(m_axis, std::string(m_slice_dlg_edit.text().toUtf8().constData()));
    close();
}
