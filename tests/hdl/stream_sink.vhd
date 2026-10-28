-- the receiving side of a stream with a data width generic, grouped into a
-- bundle port s whose data width follows TDATA_WIDTH: always ready, shows the
-- data while valid
library ieee;
use ieee.std_logic_1164.all;

entity stream_sink is
    generic (
        TDATA_WIDTH : integer := 8
    );
    port (
        s_tvalid : in  std_logic;
        s_tready : out std_logic;
        s_tdata  : in  std_logic_vector(TDATA_WIDTH - 1 downto 0);
        seen     : out std_logic_vector(TDATA_WIDTH - 1 downto 0)
    );
end entity;

architecture rtl of stream_sink is
begin
    s_tready <= '1';
    seen <= s_tdata when s_tvalid = '1' else (others => '0');
end architecture;
