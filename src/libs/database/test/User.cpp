/*
 * Copyright (C) 2024 Emeric Poupon
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

#include "Common.hpp"

namespace lms::db::tests
{
    TEST_F(DatabaseFixture, User)
    {
        {
            auto transaction{ session.createReadTransaction() };

            bool visited{};
            User::find(session, User::FindParameters{}, [&](const User::pointer&) {
                visited = true;
            });
            EXPECT_FALSE(visited);
        }

        ScopedUser user1{ session, "MyUser1" };
        ScopedUser user2{ session, "MyUser2" };

        {
            auto transaction{ session.createReadTransaction() };

            std::vector<UserId> visitedUsers;
            User::find(session, User::FindParameters{}, [&](const User::pointer& user) {
                visitedUsers.push_back(user->getId());
            });
            EXPECT_EQ(visitedUsers.size(), 2);
            EXPECT_EQ(visitedUsers[0], user1->getId());
            EXPECT_EQ(visitedUsers[1], user2->getId());
        }
    }

    TEST_F(DatabaseFixture, UserMediaLibraries)
    {
        ScopedUser user{ session, "MyUser" };
        ScopedMediaLibrary library1{ session, "Library1", "/library1" };
        ScopedMediaLibrary library2{ session, "Library2", "/library2" };

        {
            auto transaction{ session.createWriteTransaction() };
            auto userPtr{ user.get() };
            userPtr.modify()->setMediaLibraries({ library1.get() });
        }

        {
            auto transaction{ session.createReadTransaction() };
            const auto userPtr{ user.get() };
            ASSERT_EQ(userPtr->getMediaLibraries().size(), 1);
            EXPECT_TRUE(userPtr->hasMediaLibrary(library1.getId()));
            EXPECT_FALSE(userPtr->hasMediaLibrary(library2.getId()));
        }

        {
            auto transaction{ session.createWriteTransaction() };
            user.get().modify()->setMediaLibraries({ library2.get() });
        }

        {
            auto transaction{ session.createReadTransaction() };
            const auto userPtr{ user.get() };
            ASSERT_EQ(userPtr->getMediaLibraries().size(), 1);
            EXPECT_FALSE(userPtr->hasMediaLibrary(library1.getId()));
            EXPECT_TRUE(userPtr->hasMediaLibrary(library2.getId()));
        }
    }

    TEST_F(DatabaseFixture, UserNames)
    {
        ScopedUser user{ session, "MyUser" };

        {
            auto transaction{ session.createReadTransaction() };
            EXPECT_EQ(user->getLoginName(), "MyUser");
            EXPECT_EQ(user->getDisplayName(), "MyUser");
        }

        {
            auto transaction{ session.createWriteTransaction() };
            user.get().modify()->setLoginName("new-login");
            user.get().modify()->setDisplayName("New Display Name");
        }

        {
            auto transaction{ session.createReadTransaction() };
            EXPECT_EQ(user->getLoginName(), "new-login");
            EXPECT_EQ(user->getDisplayName(), "New Display Name");
        }
    }
} // namespace lms::db::tests
