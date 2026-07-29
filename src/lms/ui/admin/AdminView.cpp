/*
 * Copyright (C) 2018 Emeric Poupon
 *
 * This file is part of LMS.
 *
 * LMS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LMS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LMS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "AdminView.hpp"

#include "DebugToolsView.hpp"
#include "MediaLibrariesView.hpp"
#include "InternetRadioView.hpp"
#include "PodcastsView.hpp"
#include "ScanSettingsView.hpp"
#include "ScannerController.hpp"
#include "UserView.hpp"
#include "UsersView.hpp"
#include "common/PathRouter.hpp"

namespace lms::ui
{
    AdminView::AdminView()
    {
        auto* router{ addNew<PathRouter>() };

        router->add<MediaLibrariesView>("/admin/libraries", Wt::WString::tr("Lms.Admin.MediaLibraries.media-libraries"));
        router->add<PodcastSettingsView>("/admin/podcasts", Wt::WString::tr("Lms.Admin.Podcasts.podcasts"));
        router->add<InternetRadioView>("/admin/internet-radio", Wt::WString::tr("Lms.Admin.InternetRadio.title"));
        router->add<ScanSettingsView>("/admin/scan-settings", Wt::WString::tr("Lms.Admin.Database.scan-settings"));
        router->add<ScannerController>("/admin/scanner", Wt::WString::tr("Lms.Admin.ScannerController.scanner"));
        router->add<UsersView>("/admin/users", Wt::WString::tr("Lms.Admin.Users.users"));
        router->add<UserView>("/admin/user", std::nullopt);
        router->add<DebugToolsView>("/admin/debug-tools", Wt::WString::tr("Lms.Admin.DebugTools.debug-tools"));

        router->activate();
    }
} // namespace lms::ui
