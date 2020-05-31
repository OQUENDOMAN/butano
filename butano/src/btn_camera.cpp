#include "btn_camera.h"

#if BTN_CFG_CAMERA_ENABLED
    #include "btn_fixed_point.h"
    #include "btn_bgs_manager.h"
    #include "btn_sprites_manager.h"
    #include "btn_display_manager.h"

    namespace btn::camera
    {
        namespace
        {
            class static_data
            {

            public:
                fixed_point position;
            };

            BTN_DATA_EWRAM static_data data;
        }

        fixed x()
        {
            return data.position.x();
        }

        fixed y()
        {
            return data.position.y();
        }

        const fixed_point& position()
        {
            return data.position;
        }

        void set_x(fixed x)
        {
            set_position(fixed_point(x, data.position.y()));
        }

        void set_y(fixed y)
        {
            set_position(fixed_point(data.position.x(), y));
        }

        void set_position(fixed x, fixed y)
        {
            set_position(fixed_point(x, y));
        }

        void set_position(const fixed_point& position)
        {
            if(data.position != position)
            {
                data.position = position;
                sprites_manager::update_camera();
                bgs_manager::update_camera();
                display_manager::update_camera();
            }
        }
    }
#endif
