/*
 * Copyright (C) 1998-2017 ALPS Collaboration. See COPYRIGHT.TXT
 * All rights reserved. Use is subject to license terms. See LICENSE.TXT
 * For use in publications, see ACKNOWLEDGE.TXT
 */

/** @file params_bcast_mpi.cpp
    
    @brief Tests MPI broadcast of parameters
*/

#include "./params_test_support.hpp"
#include <alps/utilities/gtest_par_xml_output.hpp>

using alps::params;

namespace test_data {
    static const char inifile_content[]=
        "my_bool=true\n"
        "my_int=1234\n"
        "my_string=simple\n"
        "my_double=12.75\n"
        ;

}

class ParamsTest : public ::testing::Test {
  protected:
    ParamsAndFile params_and_file_;
    params& par_;
    const alps::mpi::communicator comm_;
    int root_;
    bool is_master_;
  public:
    ParamsTest() : params_and_file_(::test_data::inifile_content),
                   par_(*params_and_file_.get_params_ptr()),
                   comm_(), root_(0),
                   is_master_(comm_.rank()==root_)
                   
    {   }
};

TEST_F(ParamsTest, bcast) {
    params p_empty;
    params& p = *(is_master_ ? &par_ : &p_empty);

    par_.define<int>("my_int", "Integer param");
    par_.define<std::string>("my_string", "String param");

    if (is_master_) {
        ASSERT_TRUE(par_==p);
    } else {
        ASSERT_FALSE(par_==p);
    }
    
    using alps::mpi::broadcast;
    broadcast(comm_, p, root_);

    EXPECT_TRUE(p==par_);
}




int main(int argc, char** argv)
{
   alps::mpi::environment env(argc, argv, false);
   alps::gtest_par_xml_output tweak;
   tweak(alps::mpi::communicator().rank(), argc, argv);
   ::testing::InitGoogleTest(&argc, argv);
   
   return RUN_ALL_TESTS();
}    
