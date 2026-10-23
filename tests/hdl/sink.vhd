-- the receiving side of a valid/ready handshake, grouped into a bundle port
-- s by the cell: always ready, shows the data while valid
library ieee;
use ieee.std_logic_1164.all;

entity sink is
    port (
        s_valid : in  std_logic;
        s_ready : out std_logic;
        s_data  : in  std_logic_vector(7 downto 0);
        seen    : out std_logic_vector(7 downto 0)
    );
end entity;

architecture rtl of sink is
begin
    s_ready <= '1';
    seen <= s_data when s_valid = '1' else (others => '0');
end architecture;
