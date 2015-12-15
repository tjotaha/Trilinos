#include "gtest/gtest.h"
#include <mpi.h>
#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/ElemElemGraph.hpp>
#include <stk_mesh/base/FEMHelpers.hpp>
#include <stk_mesh/base/GetEntities.hpp>
#include <stk_unit_test_utils/MeshFixture.hpp>
#include <stk_util/parallel/CommSparse.hpp>

namespace
{

int get_other_proc(MPI_Comm comm)
{
    return stk::parallel_machine_size(comm) - stk::parallel_machine_rank(comm) - 1;
}

class CoincidentElements: public stk::unit_test_util::MeshTestFixture
{
protected:
    void make_coincident_element_mesh(unsigned numElemsToCreate, const stk::mesh::EntityIdVector &nodes, stk::mesh::Part &part)
    {
        get_bulk().modification_begin();
        declare_coincident_elements_round_robin(numElemsToCreate, nodes, part);
        make_nodes_shared(nodes);
        get_bulk().modification_end();
    }
    void run_skin_mesh()
    {
        stk::mesh::ElemElemGraph elementGraph(get_bulk(), get_meta().universal_part());
        elementGraph.skin_mesh({});
    }
    void expect_faces_connected_to_num_elements_locally(const std::vector<unsigned> &goldNumConnectedElems)
    {
        stk::mesh::EntityVector faces = get_all_faces();
        ASSERT_EQ(goldNumConnectedElems.size(), faces.size());
        for(size_t i=0; i<faces.size(); i++)
            EXPECT_EQ(goldNumConnectedElems[i], get_bulk().num_elements(faces[i])) << i;
    }
    void expect_face_ids(const stk::mesh::EntityIdVector &goldFaceIds)
    {
        stk::mesh::EntityVector faces = get_all_faces();
        ASSERT_EQ(goldFaceIds.size(), faces.size());
        for(size_t i=0; i<faces.size(); i++)
            EXPECT_EQ(goldFaceIds[i], get_bulk().identifier(faces[i])) << i;
    }
    stk::mesh::EntityVector get_all_faces()
    {
        stk::mesh::EntityVector faces;
        stk::mesh::get_selected_entities(get_meta().universal_part(), get_bulk().buckets(stk::topology::FACE_RANK), faces);
        return faces;
    }
    void make_nodes_shared(const stk::mesh::EntityIdVector &nodes)
    {
        for(const stk::mesh::EntityId nodeId : nodes)
            get_bulk().add_node_sharing(get_bulk().get_entity(stk::topology::NODE_RANK, nodeId), get_other_proc(get_comm()));
    }
private:
    bool is_element_created_on_this_proc(int elementIndex)
    {
        return (elementIndex % get_bulk().parallel_size() == get_bulk().parallel_rank());
    }
    void declare_coincident_elements_round_robin(unsigned numElemsToCreate, const stk::mesh::EntityIdVector &nodes, stk::mesh::Part &part)
    {
        for(unsigned i = 0; i < numElemsToCreate; i++)
            if(is_element_created_on_this_proc(i))
                stk::mesh::declare_element(get_bulk(), part, i+1, nodes);
    }
};

class CoincidentQuad4Shells : public CoincidentElements
{
protected:
    void skin_num_coincident_shells(unsigned numElemsToCreate, stk::mesh::BulkData::AutomaticAuraOption auraOption)
    {
        setup_empty_mesh(auraOption);
        create_stacked_shells(numElemsToCreate);
        run_skin_mesh();
    }
    void create_stacked_shells(unsigned numElemsToCreate)
    {
        stk::mesh::Part &shellPart = get_meta().get_topology_root_part(stk::topology::SHELL_QUADRILATERAL_4);
        stk::mesh::EntityIdVector nodes = {1, 2, 3, 4};
        make_coincident_element_mesh(numElemsToCreate, nodes, shellPart);
    }
    void expect_local_face_shared_to_other_proc(const std::vector<int> &goldSharedProcs, const stk::mesh::EntityIdVector &goldFaceIds)
    {
        stk::mesh::EntityVector faces = get_all_faces();
        ASSERT_EQ(goldSharedProcs.size(), faces.size());
        for(size_t i=0; i<faces.size(); i++)
            expect_face_identifier_and_sharing_proc(faces[i], goldFaceIds[i], goldSharedProcs[i]);
    }
    void expect_face_identifier_and_sharing_proc(stk::mesh::Entity face, stk::mesh::EntityId goldFaceId, int goldSharedProc)
    {
        EXPECT_EQ(goldFaceId, get_bulk().identifier(face));
        expect_sharing_proc(face, goldSharedProc);
    }
    void expect_sharing_proc(stk::mesh::Entity face, int goldSharedProc)
    {
        std::vector<int> procs;
        get_bulk().comm_shared_procs(get_bulk().entity_key(face), procs);
        ASSERT_EQ(1u, procs.size());
        EXPECT_EQ(goldSharedProc, procs[0]);
    }
};

class TwoCoincidentQuad4Shells : public CoincidentQuad4Shells
{
protected:
    virtual void run_test(stk::mesh::BulkData::AutomaticAuraOption auraOption)
    {
        unsigned numElemsToCreate = 2;
        skin_num_coincident_shells(numElemsToCreate, auraOption);
        expect_faces_connected_to_num_elements_locally({numElemsToCreate, numElemsToCreate});
    }
};
TEST_F(TwoCoincidentQuad4Shells, Skin)
{
    run_test_on_num_procs(1, stk::mesh::BulkData::NO_AUTO_AURA);
}

class ThreeCoincidentQuad4Shells : public CoincidentQuad4Shells
{
protected:
    virtual void run_test(stk::mesh::BulkData::AutomaticAuraOption auraOption)
    {
        unsigned numElemsToCreate = 3;
        skin_num_coincident_shells(numElemsToCreate, auraOption);
        expect_faces_connected_to_num_elements_locally({numElemsToCreate, numElemsToCreate});
    }
};
TEST_F(ThreeCoincidentQuad4Shells, Skin)
{
    run_test_on_num_procs(1, stk::mesh::BulkData::NO_AUTO_AURA);
}

class TwoCoincidentQuad4ShellsInParallel : public CoincidentQuad4Shells
{
protected:
    virtual void run_test(stk::mesh::BulkData::AutomaticAuraOption auraOption)
    {
        skin_num_coincident_shells(2, auraOption);
        expect_faces_connected_to_num_elements_locally({1, 1});
        int otherProc = get_other_proc(get_comm());
        expect_local_face_shared_to_other_proc({otherProc, otherProc}, {1, 2});
    }
};
TEST_F(TwoCoincidentQuad4ShellsInParallel, Skin)
{
    run_test_on_num_procs(2, stk::mesh::BulkData::NO_AUTO_AURA);
}


class CoincidentHex8s : public CoincidentElements
{
protected:
    virtual void run_test(stk::mesh::BulkData::AutomaticAuraOption auraOption)
    {
        setup_empty_mesh(auraOption);
        create_two_coincident_hexes();
        run_skin_mesh();
        expect_faces_connected_to_num_elements_locally({2, 2, 2, 2, 2, 2});
    }
    void create_two_coincident_hexes()
    {
        block1 = &get_meta().declare_part_with_topology("block_1", stk::topology::HEX_8);
        stk::mesh::EntityIdVector nodes = {1, 2, 3, 4, 5, 6, 7, 8};
        declare_num_coincident_elements(2, nodes, *block1);
    }
    void declare_num_coincident_elements(unsigned numElemsToCreate, const stk::mesh::EntityIdVector &nodes, stk::mesh::Part &part)
    {
        get_bulk().modification_begin();
        for(unsigned i = 0; i < numElemsToCreate; i++)
            stk::mesh::declare_element(get_bulk(), part, i+1, nodes);
        get_bulk().modification_end();
    }
    stk::mesh::Part *block1;
};
TEST_F(CoincidentHex8s, SkinMesh)
{
    run_test_on_num_procs(1, stk::mesh::BulkData::NO_AUTO_AURA);
}

class CoincidentHex8sWithAdjacentHex : public CoincidentHex8s
{
protected:
    void create_coincident_hex8s_with_adjacent_hex(stk::mesh::BulkData::AutomaticAuraOption auraOption)
    {
        block2 = &get_meta().declare_part_with_topology("block_2", stk::topology::HEX_8);
        setup_empty_mesh(auraOption);
        create_two_coincident_hexes();
        create_adjacent_hex();
    }
private:
    void create_adjacent_hex()
    {
        stk::mesh::EntityIdVector nodes = {5, 6, 7, 8, 9, 10, 11, 12};
        get_bulk().modification_begin();
        stk::mesh::declare_element(get_bulk(), *block2, 3, nodes);
        get_bulk().modification_end();
    }
protected:
    stk::mesh::Part *block2;
};

class CoincidentHex8sWithAdjacentHexSerial : public CoincidentHex8sWithAdjacentHex
{
    virtual void run_test(stk::mesh::BulkData::AutomaticAuraOption auraOption)
    {
        create_coincident_hex8s_with_adjacent_hex(auraOption);
        stk::mesh::ElemElemGraph elementGraph(get_bulk(), get_meta().universal_part());
        elementGraph.skin_mesh({});
        expect_faces_connected_to_num_elements_locally({1, 1, 1, 1, 1, 2, 2, 2, 2, 2});
    }
};
TEST_F(CoincidentHex8sWithAdjacentHexSerial, Skin)
{
    run_test_on_num_procs(1, stk::mesh::BulkData::NO_AUTO_AURA);
}

class CoincidentHex8sWithAdjacentAirHex : public CoincidentHex8sWithAdjacentHex
{
    virtual void run_test(stk::mesh::BulkData::AutomaticAuraOption auraOption)
    {
        create_coincident_hex8s_with_adjacent_hex(auraOption);

        stk::mesh::Selector air = *block2;
        stk::mesh::ElemElemGraph elementGraph(get_bulk(), *block1, &air);
        elementGraph.skin_mesh({});

        expect_faces_connected_to_num_elements_locally({2, 2, 2, 2, 2, 3});
    }
};
TEST_F(CoincidentHex8sWithAdjacentAirHex, Skin)
{
    run_test_on_num_procs(1, stk::mesh::BulkData::NO_AUTO_AURA);
}

class Hex8WithAdjacentCoincidentAirHex8s : public CoincidentHex8sWithAdjacentHex
{
    virtual void run_test(stk::mesh::BulkData::AutomaticAuraOption auraOption)
    {
        create_coincident_hex8s_with_adjacent_hex(auraOption);

        stk::mesh::Selector air = *block1;
        stk::mesh::ElemElemGraph elementGraph(get_bulk(), *block2, &air);
        elementGraph.skin_mesh({});

        expect_faces_connected_to_num_elements_locally({1, 1, 1, 1, 3, 1});
    }
};
TEST_F(Hex8WithAdjacentCoincidentAirHex8s, Skin)
{
    run_test_on_num_procs(1, stk::mesh::BulkData::NO_AUTO_AURA);
}

class CoincidentHex8sWithAdjacentHexInParallel : public CoincidentHex8sWithAdjacentHex
{
protected:
    void create_coincident_hex8s_with_adjacent_hex_on_2_procs(stk::mesh::BulkData::AutomaticAuraOption auraOption,
                                                              const stk::mesh::EntityIdVector &ids)
    {
        create_block_parts();
        setup_empty_mesh(auraOption);
        fill_mesh_with_coincident_hex8s_and_adjacent_hex_in_parallel(ids);
    }
    void fill_mesh_with_coincident_hex8s_and_adjacent_hex_in_parallel(const stk::mesh::EntityIdVector &ids)
    {
        get_bulk().modification_begin();
        create_coincident_hexes_on_proc0_and_adjacent_hex_on_proc1(ids);
        make_nodes_shared({5, 6, 7, 8});
        get_bulk().modification_end();
    }
    void create_block_parts()
    {
        block1 = &get_meta().declare_part_with_topology("block_1", stk::topology::HEX_8);
        block2 = &get_meta().declare_part_with_topology("block_2", stk::topology::HEX_8);
    }
    void create_coincident_hexes_on_proc0_and_adjacent_hex_on_proc1(const stk::mesh::EntityIdVector &ids)
    {
        if(get_bulk().parallel_rank() == 0)
            create_coincident_hexes(ids[0], ids[1]);
        else
            stk::mesh::declare_element(get_bulk(), *block2, ids[2], {5, 6, 7, 8, 9, 10, 11, 12});
    }
    void create_coincident_hexes(stk::mesh::EntityId id1, stk::mesh::EntityId id2)
    {
        stk::mesh::EntityIdVector nodes = {1, 2, 3, 4, 5, 6, 7, 8};
        stk::mesh::declare_element(get_bulk(), *block1, id1, nodes);
        stk::mesh::declare_element(get_bulk(), *block1, id2, nodes);
    }
    void expect_faces_connected_to_num_elements_locally_per_proc(const std::vector<unsigned> &goldNumConnectedElemsProc0,
                                                                    const std::vector<unsigned> &goldNumConnectedElemsProc1)
    {
        if(get_bulk().parallel_rank() == 0)
            expect_faces_connected_to_num_elements_locally(goldNumConnectedElemsProc0);
        else
            expect_faces_connected_to_num_elements_locally(goldNumConnectedElemsProc1);
    }
    void expect_face_ids_per_proc(const stk::mesh::EntityIdVector &goldFaceIdsProc0, const stk::mesh::EntityIdVector &goldFaceIdsProc1)
    {
        if(get_bulk().parallel_rank() == 0)
            expect_face_ids(goldFaceIdsProc0);
        else
            expect_face_ids(goldFaceIdsProc1);
    }
    void skin_part_with_part2_as_air(stk::mesh::Selector partToSkin, stk::mesh::Selector partToConsiderAsAir)
    {
        stk::mesh::ElemElemGraph elementGraph(get_bulk(), partToSkin, &partToConsiderAsAir);
        elementGraph.skin_mesh({});
    }
    stk::mesh::Part *block2;
};

class CoincidentHex8sWithAdjacentAirHexInParallel : public CoincidentHex8sWithAdjacentHexInParallel
{
protected:
    void run_with_element_ids(stk::mesh::BulkData::AutomaticAuraOption auraOption, const stk::mesh::EntityIdVector &ids)
    {
        create_coincident_hex8s_with_adjacent_hex_on_2_procs(auraOption, ids);
        skin_part_with_part2_as_air(*block1, *block2);
        expect_faces_connected_to_num_elements_locally_per_proc( {2, 2, 2, 2, 2, 2}, {1});
        expect_face_ids_per_proc({1, 3, 4, 5, 6, 7}, {1});
    }
};
TEST_F(CoincidentHex8sWithAdjacentAirHexInParallel, SkinHex1Hex2)
{
    if(stk::parallel_machine_size(get_comm()) == 2)
        run_with_element_ids(stk::mesh::BulkData::NO_AUTO_AURA, {1, 2, 3});
}
TEST_F(CoincidentHex8sWithAdjacentAirHexInParallel, SkinHex2Hex1)
{
    if(stk::parallel_machine_size(get_comm()) == 2)
        run_with_element_ids(stk::mesh::BulkData::NO_AUTO_AURA, {2, 1, 3});
}

class Hex8WithAdjacentCoincidentAirHex8sInParallel : public CoincidentHex8sWithAdjacentHexInParallel
{
    virtual void run_test(stk::mesh::BulkData::AutomaticAuraOption auraOption)
    {
        create_coincident_hex8s_with_adjacent_hex_on_2_procs(auraOption, {1, 2, 3});
        skin_part_with_part2_as_air(*block2, *block1);
        expect_faces_connected_to_num_elements_locally_per_proc({2}, {1, 1, 1, 1, 1, 1});
    }
};
TEST_F(Hex8WithAdjacentCoincidentAirHex8sInParallel, Skin)
{
    run_test_on_num_procs(2, stk::mesh::BulkData::NO_AUTO_AURA);
}

void setup_node_sharing(stk::mesh::BulkData &mesh, const std::vector< std::vector<unsigned> > & shared_nodeIDs_and_procs )
{
    const unsigned p_rank = mesh.parallel_rank();

    for (size_t nodeIdx = 0, end = shared_nodeIDs_and_procs.size(); nodeIdx < end; ++nodeIdx) {
        if (p_rank == shared_nodeIDs_and_procs[nodeIdx][0]) {
            stk::mesh::EntityId nodeID = shared_nodeIDs_and_procs[nodeIdx][1];
            int sharingProc = shared_nodeIDs_and_procs[nodeIdx][2];
            stk::mesh::Entity node = mesh.get_entity(stk::topology::NODE_RANK, nodeID);
            mesh.add_node_sharing(node, sharingProc);
        }
    }
}

class HexShellShell : public stk::unit_test_util::MeshFixture
{
protected:
    HexShellShell()
    {
        setup_empty_mesh(stk::mesh::BulkData::AUTO_AURA);
    }

    void setup_hex_shell_shell_on_procs(std::vector<int> owningProcs)
    {
        stk::mesh::Part* hexPart = &get_meta().declare_part_with_topology("hex_part", stk::topology::HEX_8);
        stk::mesh::Part* shellPart = &get_meta().declare_part_with_topology("shell_part", stk::topology::SHELL_QUAD_4);
        stk::mesh::PartVector parts = {hexPart, shellPart, shellPart};
        declare_elements_on_procs_and_setup_node_sharing(owningProcs, parts);
    }

private:
    void declare_elements_on_procs_and_setup_node_sharing(const std::vector<int>& owningProcs, const stk::mesh::PartVector& parts)
    {
        get_bulk().modification_begin();
        declare_elements_on_procs(owningProcs, parts);
        setup_node_sharing(get_bulk(), shared_nodeIDs_and_procs);
        get_bulk().modification_end();
    }

    void declare_elements_on_procs(const std::vector<int>& owningProcs, const stk::mesh::PartVector& parts)
    {
        for(size_t i = 0; i < nodeIDs.size(); ++i)
            if(owningProcs[i] == stk::parallel_machine_rank(get_comm()))
                stk::mesh::declare_element(get_bulk(), *parts[i], elemIDs[i], nodeIDs[i]);
    }

    std::vector<stk::mesh::EntityIdVector> nodeIDs { {1, 2, 3, 4, 5, 6, 7, 8}, {5, 6, 7, 8}, {5, 6, 7, 8}};
    stk::mesh::EntityIdVector elemIDs = {1, 2, 3};
    // list of triplets: (owner-proc, shared-nodeID, sharing-proc)
    std::vector<std::vector<unsigned> > shared_nodeIDs_and_procs = {
             {0, 5, 1}, {0, 6, 1}, {0, 7, 1}, {0, 8, 1},  // proc 0
             {1, 5, 0}, {1, 6, 0}, {1, 7, 0}, {1, 8, 0}}; // proc 1
};


TEST_F(HexShellShell, Hex0Shell1Shell1Parallel)
{
    //  ID.proc
    //
    //          3.0------------7.0
    //          /|             /|
    //         / |            / |
    //        /  |           /  |
    //      4.0------------8.0  |
    //       |   |          |   |
    //       |   |   1.0    |2.1|
    //       |   |          |3.1|
    //       |  2.0---------|--6.0
    //       |  /           |  /
    //       | /            | /
    //       |/             |/
    //      1.0------------5.0
    //                      ^
    //                      |
    //                       ---- Two stacked shells

    if(stk::parallel_machine_size(get_comm()) == 2u)
    {
        setup_hex_shell_shell_on_procs({0, 1, 1});

        stk::mesh::ElemElemGraph elemElemGraph(get_bulk(), get_meta().universal_part());

        if(stk::parallel_machine_rank(get_comm()) == 0)
        {
            const stk::mesh::Entity hex1 = get_bulk().get_entity(stk::topology::ELEM_RANK, 1);
            ASSERT_EQ(2u, elemElemGraph.get_num_connected_elems(hex1));
            EXPECT_EQ(2u, elemElemGraph.get_connected_remote_id_and_via_side(hex1, 0).id);
            EXPECT_EQ(5, elemElemGraph.get_connected_remote_id_and_via_side(hex1, 0).side);
            EXPECT_EQ(3u, elemElemGraph.get_connected_remote_id_and_via_side(hex1, 1).id);
            EXPECT_EQ(5, elemElemGraph.get_connected_remote_id_and_via_side(hex1, 1).side);
            EXPECT_FALSE(elemElemGraph.is_connected_elem_locally_owned(hex1, 0));
            EXPECT_FALSE(elemElemGraph.is_connected_elem_locally_owned(hex1, 1));
            EXPECT_EQ(2u, elemElemGraph.num_edges());
            EXPECT_EQ(2u, elemElemGraph.num_parallel_edges());
        }
        else
        {
            const stk::mesh::Entity shell2 = get_bulk().get_entity(stk::topology::ELEM_RANK, 2);
            ASSERT_EQ(1u, elemElemGraph.get_num_connected_elems(shell2));
            EXPECT_EQ(1, elemElemGraph.get_connected_remote_id_and_via_side(shell2, 0).side);
            EXPECT_EQ(1u, elemElemGraph.get_connected_remote_id_and_via_side(shell2, 0).id);
            EXPECT_FALSE(elemElemGraph.is_connected_elem_locally_owned(shell2, 0));

            const stk::mesh::Entity shell3 = get_bulk().get_entity(stk::topology::ELEM_RANK, 3);
            ASSERT_EQ(1u, elemElemGraph.get_num_connected_elems(shell3));
            EXPECT_EQ(1, elemElemGraph.get_connected_remote_id_and_via_side(shell3, 0).side);
            EXPECT_EQ(1u, elemElemGraph.get_connected_remote_id_and_via_side(shell3, 0).id);
            EXPECT_FALSE(elemElemGraph.is_connected_elem_locally_owned(shell3, 0));
        }
    }
}

TEST_F(HexShellShell, Hex0Shell0Shell1Parallel )
{
    //  ID.proc
    //
    //          3.0------------7.0
    //          /|             /|
    //         / |            / |
    //        /  |           /  |
    //      4.0------------8.0  |
    //       |   |          |   |
    //       |   |   1.0    |2.0|
    //       |   |          |3.1|
    //       |  2.0---------|--6.0
    //       |  /           |  /
    //       | /            | /
    //       |/             |/
    //      1.0------------5.0
    //                      ^
    //                      |
    //                       ---- Two stacked shells

    if(stk::parallel_machine_size(get_comm()) == 2u)
    {
        setup_hex_shell_shell_on_procs({0, 0, 1});

        stk::mesh::ElemElemGraph elemElemGraph(get_bulk(), get_meta().universal_part());

        if(stk::parallel_machine_rank(get_comm()) == 0)
        {
            const stk::mesh::Entity hex1 = get_bulk().get_entity(stk::topology::ELEM_RANK, 1);
            const stk::mesh::Entity shell2 = get_bulk().get_entity(stk::topology::ELEM_RANK, 2);
            ASSERT_EQ(2u, elemElemGraph.get_num_connected_elems(hex1));
            ASSERT_TRUE(elemElemGraph.is_connected_elem_locally_owned(hex1, 0));
            EXPECT_EQ(5, elemElemGraph.get_connected_element_and_via_side(hex1, 0).side);
            EXPECT_EQ(shell2, elemElemGraph.get_connected_element_and_via_side(hex1, 0).element);
            ASSERT_TRUE(!elemElemGraph.is_connected_elem_locally_owned(hex1, 1));
            EXPECT_EQ(3u, elemElemGraph.get_connected_remote_id_and_via_side(hex1, 1).id);
            EXPECT_EQ(5, elemElemGraph.get_connected_remote_id_and_via_side(hex1, 1).side);

            ASSERT_EQ(1u, elemElemGraph.get_num_connected_elems(shell2));
            EXPECT_EQ(1, elemElemGraph.get_connected_element_and_via_side(shell2, 0).side);
            EXPECT_EQ(hex1, elemElemGraph.get_connected_element_and_via_side(shell2, 0).element);
            EXPECT_TRUE(elemElemGraph.is_connected_elem_locally_owned(shell2, 0));
            EXPECT_EQ(3u, elemElemGraph.num_edges());
            EXPECT_EQ(1u, elemElemGraph.num_parallel_edges());
        }
        else
        {
            const stk::mesh::Entity shell3 = get_bulk().get_entity(stk::topology::ELEM_RANK, 3);
            ASSERT_EQ(1u, elemElemGraph.get_num_connected_elems(shell3));
            EXPECT_EQ(1, elemElemGraph.get_connected_remote_id_and_via_side(shell3, 0).side);
            EXPECT_EQ(1u, elemElemGraph.get_connected_remote_id_and_via_side(shell3, 0).id);
            EXPECT_FALSE(elemElemGraph.is_connected_elem_locally_owned(shell3, 0));
            EXPECT_EQ(1u, elemElemGraph.num_edges());
            EXPECT_EQ(1u, elemElemGraph.num_parallel_edges());
        }
    }
}

}
