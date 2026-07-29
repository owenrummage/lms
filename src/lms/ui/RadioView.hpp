/*
 * Copyright (C) 2026 Emeric Poupon
 *
 * This file is part of LMS.
 */

#pragma once

#include <Wt/WContainerWidget.h>

namespace lms::ui
{
    class RadioView final : public Wt::WContainerWidget
    {
    public:
        RadioView();

    private:
        void refreshView();

        Wt::WContainerWidget* _stations{};
    };
} // namespace lms::ui
